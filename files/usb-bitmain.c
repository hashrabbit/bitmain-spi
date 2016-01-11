/*
 * USB Bitmain driver - 2.2
 *
 * Copyright (C) 2001-2004 Greg Kroah-Hartman (greg@kroah.com)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 * This driver is based on the 2.6.3 version of usb-bitmain.c
 * but has been rewritten to be easier to read and use.
 *
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kref.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/mutex.h>


/* Define these values to match bitmain asic devices */
#define IDVENDOR_FTDI 0x0403
#define IDPRODUCT_FTDI 0x6001


// For 0x10c4:0xea60 USB cp210x chip
#define CP210X_TYPE_OUT 0x41

#define CP210X_REQUEST_IFC_ENABLE 0x00
#define CP210X_REQUEST_DATA 0x07
#define CP210X_REQUEST_BAUD 0x1e

#define CP210X_VALUE_UART_ENABLE 0x0001
#define CP210X_VALUE_DATA 0x0303
#define CP210X_DATA_BAUD 0x0001c200

#define IDVENDOR_CP210X 0x10c4
#define IDPRODUCT_CP210X 0xea60

#define IDVENDOR_PIC	0x4254
#define IDPRODUCT_PIC	0x4153



/* table of devices that work with this driver */
static const struct usb_device_id bitmain_table[] = {
	{ USB_DEVICE(IDVENDOR_FTDI, IDPRODUCT_FTDI) },
	{ USB_DEVICE(IDVENDOR_CP210X, IDPRODUCT_CP210X) },
	{ USB_DEVICE(IDVENDOR_PIC, IDPRODUCT_PIC) },
	{ }					/* Terminating entry */
};
MODULE_DEVICE_TABLE(usb, bitmain_table);


/* Get a minor range for your devices from the usb maintainer */
#define USB_BTM_MINOR_BASE	0

/* our private defines. if this grows any larger, use your own .h file */
#define MAX_TRANSFER		(PAGE_SIZE - 512)
/* MAX_TRANSFER is chosen so that the VM is not stressed by
   allocations > PAGE_SIZE and the number of packets in a page
   is an integer 512 is the largest possible packet on EHCI */
#define WRITES_IN_FLIGHT	8
/* arbitrarily chosen */

/* Structure to hold all of our device specific stuff */
struct usb_bitmain {
	struct usb_device	*udev;			/* the usb device for this device */
	struct usb_interface	*interface;		/* the interface for this device */
	struct semaphore	limit_sem;		/* limiting the number of writes in progress */
	struct usb_anchor	submitted;		/* in case we need to retract our submissions */
	struct urb		*bulk_in_urb;		/* the urb to read data with */
	struct urb		*bulk_out_urb;		/* the urb to write data with */
	unsigned char           *bulk_in_buffer;	/* the buffer to receive data */
	size_t			bulk_in_size;		/* the size of the receive buffer */
	size_t			bulk_in_filled;		/* number of bytes in the buffer */
	size_t			bulk_in_copied;		/* already copied to user space */
	__u8			bulk_in_endpointAddr;	/* the address of the bulk in endpoint */
	#if 0
	unsigned char   *bulk_out_buffer;	/* the buffer to send data */
	size_t			bulk_out_size;		/* the size of the send buffer */
	size_t			bulk_out_filled;		/* number of bytes out the buffer */
	size_t			bulk_out_copied;		/* already copied from user space */
	__u8			bulk_out_endpointAddr;	/* the address of the bulk out endpoint */
	dma_addr_t 		out_transfer_dma;	/* (out) dma addr for transfer_buffer */
	#endif
	__u8			bulk_out_endpointAddr;	/* the address of the bulk out endpoint */
	int			errors;			/* the last request tanked */
	bool			ongoing_read;		/* a read is going on */
	spinlock_t		err_lock;		/* lock for errors */
	struct kref		kref;
	struct mutex		io_mutex;		/* synchronize I/O with disconnect */
	wait_queue_head_t	bulk_in_wait;		/* to wait for an ongoing read */
};
#define to_bitmain_dev(d) container_of(d, struct usb_bitmain, kref)

static struct usb_driver bitmain_driver;
static void bitmain_draw_down(struct usb_bitmain *dev);

static void bitmain_delete(struct kref *kref)
{
	struct usb_bitmain *dev = to_bitmain_dev(kref);

	usb_put_dev(dev->udev);
	usb_free_urb(dev->bulk_in_urb);
	kfree(dev->bulk_in_buffer);
	#if 0
	usb_free_urb(dev->bulk_in_urb);
	/* free up our allocated out buffer */
	usb_free_coherent(dev->udev, dev->bulk_out_size,
			  dev->bulk_out_buffer, dev->out_transfer_dma);
	#endif
	kfree(dev);
}

static int bitmain_open(struct inode *inode, struct file *file)
{
	struct usb_bitmain *dev;
	struct usb_interface *interface;
	int subminor;
	int retval = 0;

	subminor = iminor(inode);

	interface = usb_find_interface(&bitmain_driver, subminor);
	if (!interface) {
		pr_err("%s - error, can't find device for minor %d\n",
			__func__, subminor);
		retval = -ENODEV;
		goto exit;
	}

	dev = usb_get_intfdata(interface);
	if (!dev) {
		retval = -ENODEV;
		goto exit;
	}

	retval = usb_autopm_get_interface(interface);
	if (retval)
		goto exit;

	/* increment our usage count for the device */
	kref_get(&dev->kref);

	/* save our object in the file's private structure */
	file->private_data = dev;

exit:
	return retval;
}

static int bitmain_release(struct inode *inode, struct file *file)
{
	struct usb_bitmain *dev;

	dev = file->private_data;
	if (dev == NULL)
		return -ENODEV;

	/* allow the device to be autosuspended */
	mutex_lock(&dev->io_mutex);
	if (dev->interface)
		usb_autopm_put_interface(dev->interface);
	mutex_unlock(&dev->io_mutex);

	/* decrement the count on our device */
	kref_put(&dev->kref, bitmain_delete);
	return 0;
}

static int bitmain_flush(struct file *file, fl_owner_t id)
{
	struct usb_bitmain *dev;
	int res;

	dev = file->private_data;
	if (dev == NULL)
		return -ENODEV;

	/* wait for io to stop */
	mutex_lock(&dev->io_mutex);
	bitmain_draw_down(dev);

	/* read out errors, leave subsequent opens a clean slate */
	spin_lock_irq(&dev->err_lock);
	res = dev->errors ? (dev->errors == -EPIPE ? -EPIPE : -EIO) : 0;
	dev->errors = 0;
	spin_unlock_irq(&dev->err_lock);

	mutex_unlock(&dev->io_mutex);

	return res;
}

static void bitmain_read_bulk_callback(struct urb *urb)
{
	struct usb_bitmain *dev;

	dev = urb->context;

	spin_lock(&dev->err_lock);
	/* sync/async unlink faults aren't errors */
	if (urb->status) {
		if (!(urb->status == -ENOENT ||
		    urb->status == -ECONNRESET ||
		    urb->status == -ESHUTDOWN))
			dev_err(&dev->interface->dev,
				"%s - nonzero write bulk status received: %d\n",
				__func__, urb->status);

		dev->errors = urb->status;
	} else {
		printk("read callback leng{%d}\n", urb->actual_length);
		dev->bulk_in_filled = urb->actual_length;
	}
	dev->ongoing_read = 0;
	spin_unlock(&dev->err_lock);

	wake_up_interruptible(&dev->bulk_in_wait);
}

static int bitmain_do_read_io(struct usb_bitmain *dev, size_t count)
{
	int rv;

	/* prepare a read */
	usb_fill_bulk_urb(dev->bulk_in_urb,
			dev->udev,
			usb_rcvbulkpipe(dev->udev,
				dev->bulk_in_endpointAddr),
			dev->bulk_in_buffer,
			min(dev->bulk_in_size, count),
			bitmain_read_bulk_callback,
			dev);
	/* tell everybody to leave the URB alone */
	spin_lock_irq(&dev->err_lock);
	dev->ongoing_read = 1;
	spin_unlock_irq(&dev->err_lock);

	/* submit bulk in urb, which means no data to deliver */
	dev->bulk_in_filled = 0;
	dev->bulk_in_copied = 0;

	/* do it */
	rv = usb_submit_urb(dev->bulk_in_urb, GFP_KERNEL);
	if (rv < 0) {
		dev_err(&dev->interface->dev,
			"%s - failed submitting read urb, error %d\n",
			__func__, rv);
		rv = (rv == -ENOMEM) ? rv : -EIO;
		spin_lock_irq(&dev->err_lock);
		dev->ongoing_read = 0;
		spin_unlock_irq(&dev->err_lock);
	}

	return rv;
}

static ssize_t bitmain_read(struct file *file, char *buffer, size_t count,
			 loff_t *ppos)
{
	struct usb_bitmain *dev;
	int rv;
	bool ongoing_io;

	dev = file->private_data;

	/* if we cannot read at all, return EOF */
	if (!dev->bulk_in_urb || !count)
		return 0;

	/* no concurrent readers */
	rv = mutex_lock_interruptible(&dev->io_mutex);
	if (rv < 0)
		return rv;

	if (!dev->interface) {		/* disconnect() was called */
		rv = -ENODEV;
		goto exit;
	}

	if((rv = usb_bulk_msg(dev->udev, usb_rcvbulkpipe(dev->udev, dev->bulk_in_endpointAddr),
		 dev->bulk_in_buffer, dev->bulk_in_size, &dev->bulk_in_filled, 4)) < 0)
	{
		if(rv != -ETIMEDOUT)
			printk("in %s usb_bulk_msg error status {%d}\n", __func__, rv);

	}
	else
	{
		rv = dev->bulk_in_filled;
		if(access_ok(VERIFY_WRITE, buffer, count) == 0)
		{
			printk("read %d bytes\n", rv);
			memcpy(buffer, dev->bulk_in_buffer,rv);
		}
		else
		if (copy_to_user(buffer, dev->bulk_in_buffer,rv))
			rv = -EFAULT;
	}
	#if 0
	/* if IO is under way, we must not touch things */
retry:
	spin_lock_irq(&dev->err_lock);
	ongoing_io = dev->ongoing_read;
	spin_unlock_irq(&dev->err_lock);

	if (ongoing_io) {
		/* nonblocking IO shall not wait */
		if (file->f_flags & O_NONBLOCK) {
			rv = -EAGAIN;
			goto exit;
		}
		/*
		 * IO may take forever
		 * hence wait in an interruptible state
		 */
		rv = wait_event_interruptible(dev->bulk_in_wait, (!dev->ongoing_read));
		if (rv < 0)
			goto exit;
	}

	/* errors must be reported */
	rv = dev->errors;
	if (rv < 0) {
		/* any error is reported once */
		dev->errors = 0;
		/* to preserve notifications about reset */
		rv = (rv == -EPIPE) ? rv : -EIO;
		/* report it */
		goto exit;
	}

	/*
	 * if the buffer is filled we may satisfy the read
	 * else we need to start IO
	 */

	if (dev->bulk_in_filled) {
		/* we had read data */
		size_t available = dev->bulk_in_filled - dev->bulk_in_copied;
		size_t chunk = min(available, count);

		if (!available) {
			/*
			 * all data has been used
			 * actual IO needs to be done
			 */
			 printk("available no\n");
			rv = bitmain_do_read_io(dev, count);
			if (rv < 0)
				goto exit;
			else
				goto retry;
		}
		/*
		 * data is available
		 * chunk tells us how much shall be copied
		 */
		if(1/*access_ok(VERIFY_WRITE, buffer, count) == 0*/)
		{
			printk("read %d bytes\n", chunk);
			memcpy(buffer, dev->bulk_in_buffer + dev->bulk_in_copied,
				 chunk);
			rv = chunk;
		}
		else
		if (copy_to_user(buffer,
				 dev->bulk_in_buffer + dev->bulk_in_copied,
				 chunk))
			rv = -EFAULT;
		else
			rv = chunk;

		dev->bulk_in_copied += chunk;

		/*
		 * if we are asked for more than we have,
		 * we start IO but don't wait
		 */
		if (available < count)
		{
			printk("continue usb read\n");
			bitmain_do_read_io(dev, count - chunk);
		}
	} else {
		/* no data in the buffer */
		printk("no bulk in filled\n");
		rv = bitmain_do_read_io(dev, count);
		if (rv < 0)
			goto exit;
		else if (!(file->f_flags & O_NONBLOCK))
			goto retry;
		rv = -EAGAIN;
	}
	#endif
exit:
	mutex_unlock(&dev->io_mutex);
	return rv;
}

static void bitmain_write_bulk_callback(struct urb *urb)
{
	struct usb_bitmain *dev;

	dev = urb->context;

	/* sync/async unlink faults aren't errors */
	if (urb->status) {
		if (!(urb->status == -ENOENT ||
		    urb->status == -ECONNRESET ||
		    urb->status == -ESHUTDOWN))
			dev_err(&dev->interface->dev,
				"%s - nonzero write bulk status received: %d\n",
				__func__, urb->status);

		spin_lock(&dev->err_lock);
		dev->errors = urb->status;
		spin_unlock(&dev->err_lock);
	}
	/*
	else
	{
		printk("bitmain_write_bulk_callback ok\n");
	}
	*/
	/* free up our allocated buffer */
	usb_free_coherent(urb->dev, urb->transfer_buffer_length,
			  urb->transfer_buffer, urb->transfer_dma);
	up(&dev->limit_sem);
}

static ssize_t bitmain_write(struct file *file, const char *user_buffer,
			  size_t count, loff_t *ppos)
{
	struct usb_bitmain *dev;
	int retval = 0;
	struct urb *urb = NULL;
	char *buf = NULL;
	size_t writesize = min(count, (size_t)MAX_TRANSFER);

	dev = file->private_data;

	/* verify that we actually have some data to write */
	if (count == 0)
		goto exit;

	/*
	 * limit the number of URBs in flight to stop a user from using up all
	 * RAM
	 */
	if (!(file->f_flags & O_NONBLOCK)) {
		if (down_interruptible(&dev->limit_sem)) {
			retval = -ERESTARTSYS;
			goto exit;
		}
	} else {
		if (down_trylock(&dev->limit_sem)) {
			retval = -EAGAIN;
			goto exit;
		}
	}

	spin_lock_irq(&dev->err_lock);
	retval = dev->errors;
	if (retval < 0) {
		/* any error is reported once */
		dev->errors = 0;
		/* to preserve notifications about reset */
		retval = (retval == -EPIPE) ? retval : -EIO;
	}
	spin_unlock_irq(&dev->err_lock);
	if (retval < 0)
		goto error;

	/* create a urb, and a buffer for it, and copy the data to the urb */

	urb = usb_alloc_urb(0, GFP_KERNEL);
	if (!urb) {
		retval = -ENOMEM;
		goto error;
	}

	buf = usb_alloc_coherent(dev->udev, writesize, GFP_KERNEL,
				 &urb->transfer_dma);
	if (!buf) {
		retval = -ENOMEM;
		goto error;
	}
	#if 0
	urb = dev->bulk_out_urb;
	buf = dev->bulk_out_buffer;
	urb->transfer_dma = dev->out_transfer_dma;
	#endif
	if(access_ok(VERIFY_READ, user_buffer, count) == 0)
	{
		memcpy(buf, user_buffer, writesize);
		//printk("memcpy\n");
	}
	else
	if (copy_from_user(buf, user_buffer, writesize)) {
		retval = -EFAULT;
		goto error;
	}

	/* this lock makes sure we don't submit URBs to gone devices */
	mutex_lock(&dev->io_mutex);
	if (!dev->interface) {		/* disconnect() was called */
		mutex_unlock(&dev->io_mutex);
		retval = -ENODEV;
		goto error;
	}

	/* initialize the urb properly */
	usb_fill_bulk_urb(urb, dev->udev,
			  usb_sndbulkpipe(dev->udev, dev->bulk_out_endpointAddr),
			  buf, writesize, bitmain_write_bulk_callback, dev);
	urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;
	usb_anchor_urb(urb, &dev->submitted);

	/* send the data out the bulk port */
	retval = usb_submit_urb(urb, GFP_KERNEL);
	mutex_unlock(&dev->io_mutex);
	if (retval) {
		dev_err(&dev->interface->dev,
			"%s - failed submitting write urb, error %d\n",
			__func__, retval);
		goto error_unanchor;
	}
	/*
	else
	{
		printk("usb_submit_urb ok\n");
	}
	*/
	/*
	 * release our reference to this urb, the USB core will eventually free
	 * it entirely
	 */
	usb_free_urb(urb);

	return writesize;

error_unanchor:
	usb_unanchor_urb(urb);
error:
	if (urb) {
		usb_free_coherent(dev->udev, writesize, buf, urb->transfer_dma);
		usb_free_urb(urb);
	}
	up(&dev->limit_sem);

exit:
	return retval;
}

static const struct file_operations bitmain_fops = {
	.owner =	THIS_MODULE,
	.read =		bitmain_read,
	.write =	bitmain_write,
	.open =		bitmain_open,
	.release =	bitmain_release,
	.flush =	bitmain_flush,
	.llseek =	noop_llseek,
};

struct file_operations *bitmain_fops_p = NULL;

EXPORT_SYMBOL_GPL(bitmain_fops_p);

/*
 * usb class driver info in order to get a minor number from the usb core,
 * and to have the device registered with the driver core
 */
static struct usb_class_driver bitmain_class = {
	.name =		"bitmain%d",
	.fops =		&bitmain_fops,
	.minor_base =	USB_BTM_MINOR_BASE,
};

static int bitmain_probe(struct usb_interface *interface,
		      const struct usb_device_id *id)
{
	struct usb_bitmain *dev;
	struct usb_host_interface *iface_desc;
	struct usb_endpoint_descriptor *endpoint;
	size_t buffer_size;
	int i;
	int retval = -ENOMEM;

	/* allocate memory for our device state and initialize it */
	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev) {
		dev_err(&interface->dev, "Out of memory\n");
		goto error;
	}
	kref_init(&dev->kref);
	sema_init(&dev->limit_sem, WRITES_IN_FLIGHT);
	mutex_init(&dev->io_mutex);
	spin_lock_init(&dev->err_lock);
	init_usb_anchor(&dev->submitted);
	init_waitqueue_head(&dev->bulk_in_wait);

	dev->udev = usb_get_dev(interface_to_usbdev(interface));
	dev->interface = interface;

	/* set up the endpoint information */
	/* use only the first bulk-in and bulk-out endpoints */
	iface_desc = interface->cur_altsetting;
	for (i = 0; i < iface_desc->desc.bNumEndpoints; ++i) {
		endpoint = &iface_desc->endpoint[i].desc;

		if (!dev->bulk_in_endpointAddr &&
		    usb_endpoint_is_bulk_in(endpoint)) {
			/* we found a bulk in endpoint */
			buffer_size = usb_endpoint_maxp(endpoint);
			dev->bulk_in_size = buffer_size;
			dev->bulk_in_endpointAddr = endpoint->bEndpointAddress;
			dev->bulk_in_buffer = kmalloc(buffer_size, GFP_KERNEL);
			if (!dev->bulk_in_buffer) {
				dev_err(&interface->dev,
					"Could not allocate bulk_in_buffer\n");
				goto error;
			}
			dev->bulk_in_urb = usb_alloc_urb(0, GFP_KERNEL);
			if (!dev->bulk_in_urb) {
				dev_err(&interface->dev,
					"Could not allocate bulk_in_urb\n");
				goto error;
			}
		}

		if (!dev->bulk_out_endpointAddr &&
		    usb_endpoint_is_bulk_out(endpoint)) {
			/* we found a bulk out endpoint */
			#if 0
			buffer_size = usb_endpoint_maxp(endpoint);
			dev->bulk_out_size = buffer_size;

			//dev->bulk_out_buffer = kmalloc(buffer_size, GFP_KERNEL);
			dev->bulk_out_buffer = usb_alloc_coherent(dev->udev, buffer_size, GFP_KERNEL,
				 &dev->out_transfer_dma);
			if (!dev->bulk_out_buffer) {
				dev_err(&interface->dev,
					"Could not allocate bulk_out_buffer\n");
				goto error;
			}
			dev->bulk_out_urb = usb_alloc_urb(0, GFP_KERNEL);
			if (!dev->bulk_out_urb) {
				dev_err(&interface->dev,
					"Could not allocate bulk_out_urb\n");
				goto error;
			}
			#endif
			dev->bulk_out_endpointAddr = endpoint->bEndpointAddress;
		}
	}
	if (!(dev->bulk_in_endpointAddr && dev->bulk_out_endpointAddr)) {
		dev_err(&interface->dev,
			"Could not find both bulk-in and bulk-out endpoints\n");
		goto error;
	}

	/* save our data pointer in this interface device */
	usb_set_intfdata(interface, dev);

	/* we can register the device now, as it is ready */
	retval = usb_register_dev(interface, &bitmain_class);
	if (retval) {
		/* something prevented us from registering this driver */
		dev_err(&interface->dev,
			"Not able to get a minor for this device.\n");
		usb_set_intfdata(interface, NULL);
		goto error;
	}

	/* let the user know what node this device is now attached to */
	dev_info(&interface->dev,
		 "USB Bitmain asic device now attached to USB Bitmain asic-%d",
		 interface->minor);

	if(id->idVendor == IDVENDOR_CP210X)
	{
		// Enable the UART
		retval = usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev,0),CP210X_REQUEST_IFC_ENABLE,
			CP210X_TYPE_OUT, CP210X_VALUE_UART_ENABLE,0,NULL,0,0);
		if(retval < 0)
		{
			printk("Enable ther UART error\n");
			goto error;
		}
		// Set data control
		retval = usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev,0),CP210X_REQUEST_DATA,
			CP210X_TYPE_OUT, CP210X_VALUE_DATA, 0,NULL,0,0);
		if(retval < 0)
		{
			printk("Set data control error\n");
			goto error;
		}
		// Set the baud
		uint32_t data = cpu_to_le32(CP210X_DATA_BAUD);
		retval = usb_control_msg(dev->udev, usb_sndctrlpipe(dev->udev,0), CP210X_REQUEST_BAUD,
			 CP210X_TYPE_OUT,0, 0, &data, sizeof(data),0);
		if(retval < 0)
		{
			printk("Set data control error\n");
			goto error;
		}

		printk("Usb to 232 haved OK\n");
	}
	else if(id->idVendor == IDVENDOR_PIC)
	{
		printk("PIC microchip usb OK\n");
	}
	bitmain_fops_p = &bitmain_fops;
	return 0;

error:
	if (dev)
		/* this frees allocated memory */
		kref_put(&dev->kref, bitmain_delete);
	return retval;
}

static void bitmain_disconnect(struct usb_interface *interface)
{
	struct usb_bitmain *dev;
	int minor = interface->minor;

	bitmain_fops_p = NULL;
	dev = usb_get_intfdata(interface);
	usb_set_intfdata(interface, NULL);

	/* give back our minor */
	usb_deregister_dev(interface, &bitmain_class);

	/* prevent more I/O from starting */
	mutex_lock(&dev->io_mutex);
	dev->interface = NULL;
	mutex_unlock(&dev->io_mutex);

	usb_kill_anchored_urbs(&dev->submitted);

	/* decrement our usage count */
	kref_put(&dev->kref, bitmain_delete);

	dev_info(&interface->dev, "USB Bitmain asic #%d now disconnected", minor);
}

static void bitmain_draw_down(struct usb_bitmain *dev)
{
	int time;

	time = usb_wait_anchor_empty_timeout(&dev->submitted, 1000);
	if (!time)
		usb_kill_anchored_urbs(&dev->submitted);
	usb_kill_urb(dev->bulk_in_urb);
}

static int bitmain_suspend(struct usb_interface *intf, pm_message_t message)
{
	struct usb_bitmain *dev = usb_get_intfdata(intf);

	if (!dev)
		return 0;
	bitmain_draw_down(dev);
	return 0;
}

static int bitmain_resume(struct usb_interface *intf)
{
	return 0;
}

static int bitmain_pre_reset(struct usb_interface *intf)
{
	struct usb_bitmain *dev = usb_get_intfdata(intf);

	mutex_lock(&dev->io_mutex);
	bitmain_draw_down(dev);

	return 0;
}

static int bitmain_post_reset(struct usb_interface *intf)
{
	struct usb_bitmain *dev = usb_get_intfdata(intf);

	/* we are sure no URBs are active - no locking needed */
	dev->errors = -EPIPE;
	mutex_unlock(&dev->io_mutex);

	return 0;
}

static struct usb_driver bitmain_driver = {
	.name =		"bitmain",
	.probe =	bitmain_probe,
	.disconnect =	bitmain_disconnect,
	.suspend =	bitmain_suspend,
	.resume =	bitmain_resume,
	.pre_reset =	bitmain_pre_reset,
	.post_reset =	bitmain_post_reset,
	.id_table =	bitmain_table,
	.supports_autosuspend = 1,
};

module_usb_driver(bitmain_driver);

MODULE_LICENSE("GPL");
