//OPTION 1 - create a virtual meta-device, rx-device, and tx-device
// then map read/write/ioctl for meta back to real-device, and buffer rx/tx

//OPTION 2 - override i_fops for read_device, buffer rx/tx
#include "uchar_queue.h"

#include <asm/uaccess.h>	/* for put_user */

#include <linux/circ_buf.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/kdev_t.h>
#include <linux/module.h>
#include <linux/namei.h>
#include <linux/slab.h>
#include <linux/kernel.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("RSAXVC");	/* Who wrote this module? */
MODULE_DESCRIPTION("Serial Port Multiplexing Hax");	/* What does this module do */

#define SUCCESS 0
#define ERROR -1
#define BUF_LEN 80
#define DEVICE_NAME "ttySnoop"

static const char *suffixes[]={"_meta","_rx","_tx"};

enum
	{
	MINOR_META,
	MINOR_RX,
	MINOR_TX,
	MINOR_COUNT
	};

/*
 * Global variables are declared as static, so are global within the file.
 */
static int Major;		/* Major number assigned to our device driver */

/*
 * Device variables - these could probably be put into a single struct if we support multiple devs later
 */

/*At most one of each can be opened*/
static int Device_Open[MINOR_COUNT];

static struct class *dev_Class;
static struct device *chr_dev[MINOR_COUNT];


/*queues for virtual ports*/
static uchar_queue rx_queue;
static uchar_queue tx_queue;

/*buffer size*/
static int buffer_sz = 1024; /*I wanted size_t, but module_param didn't like it*/
module_param(buffer_sz, int, 0444 );
MODULE_PARM_DESC( buffer_sz, "number of bytes we'll store for another process before dropping");

/*port names*/
static char * target_ports[ MINOR_COUNT ];/*snooped meta-device*/
static char * target_port = "/dev/null";/*actual device*/
module_param( target_port, charp, 0444 );
MODULE_PARM_DESC( target_port, "Takes the /dev/""* port to mount" );

static ssize_t device_read(struct file * fp, char __user * up, size_t sz, loff_t * off);
static ssize_t device_write(struct file * fp, const char __user * up, size_t sz, loff_t * off);
static int device_open(struct inode * ip, struct file * fp);
static int device_release(struct inode * ip, struct file * fp);

static ssize_t target_read(struct file * fp, char __user * up, size_t sz, loff_t * off);
static ssize_t target_write(struct file * fp, const char __user * up, size_t sz, loff_t * off);
static int target_release(struct inode * ip, struct file * fp);

static void print_fops( const struct file_operations * fops );

/*fops*/
struct file_operations their_fops;
struct file_operations our_fops = {
	.read = device_read,
	.write = device_write,
	.open = device_open,
	.release = device_release
};
const struct file_operations * their_orig_fops;

static ssize_t device_read(struct file * fp, char __user * up, size_t sz, loff_t * off)
{
size_t out_sz = 0;

if( MINOR( fp->f_dentry->d_inode->i_rdev ) > MINOR_COUNT )
	{
	printk( KERN_ALERT "Bad Minor number in inode\n" );
	return out_sz;
	}

switch( MINOR( fp->f_dentry->d_inode->i_rdev ) )
	{
	case MINOR_META:
		//
		break;

	case MINOR_RX:
		while( sz && queue_size( &rx_queue ) )
			{
			put_user( queue_pop( &rx_queue ), up++ );
			out_sz++;
			}
		break;

	case MINOR_TX:
		while( sz && queue_size( &tx_queue ) )
			{
			put_user( queue_pop( &tx_queue ), up++ );
			out_sz++;
			}
		break;
	}

return 0;
}

static ssize_t device_write(struct file * fp, const char __user * up, size_t sz, loff_t * off)
{
if( MAJOR( fp->f_dentry->d_inode->i_rdev ) != Major )
	{
	printk( KERN_ALERT "Bad Major number in inode\n" );
	return 0;
	}

switch( MINOR( fp->f_dentry->d_inode->i_rdev ) == 0 )
	{
	case MINOR_META:
		printk( KERN_ALERT "write not implemented\n" );
		return sz;

	case MINOR_RX:
		/*a bit of a lie, but we want this device to look just like a real one*/
		printk( KERN_INFO "write not implemented\n" );
		return sz;

	case MINOR_TX:
		/*a bit of a lie, but we want this device to look just like a real one*/
		printk( KERN_INFO "write not implemented\n" );
		return sz;
	}

return -EINVAL;
}

static int device_open(struct inode * ip, struct file * fp)
{
if( Device_Open[ MINOR( fp->f_dentry->d_inode->i_rdev ) ] == 0 )
	{
	Device_Open[ MINOR( fp->f_dentry->d_inode->i_rdev ) ]++;
	return SUCCESS;
	}
else
	{
	return ERROR;
	}
}

static int device_release(struct inode * ip, struct file * fp)
{
if( Device_Open[ MINOR( fp->f_dentry->d_inode->i_rdev ) ] == 0 )
	{
	Device_Open[ MINOR( fp->f_dentry->d_inode->i_rdev ) ]--;
	return SUCCESS;
	}
else
	{
	return ERROR;
	}
}

static int target_release(struct inode * ip, struct file * fp)
{
printk( KERN_INFO "RELEASE CALLED" );
return their_orig_fops->release( ip, fp );
}

static ssize_t target_read(struct file * fp, char __user * up, size_t sz, loff_t * off)
{
printk( KERN_INFO "READ CALLED" );
return their_orig_fops->read( fp, up, sz, off );
}

static ssize_t target_write(struct file * fp, const char __user * up, size_t sz, loff_t * off)
{
printk( KERN_INFO "WRITE CALLED" );
return their_orig_fops->write( fp, up, sz, off );
}

int init_module(void)
{
struct path p;
int i;
struct inode * inod;


printk(KERN_INFO "init_module() called\n");

/*generate all the device name strings we'll need*/
for( i = 0; i < MINOR_COUNT; ++i )
	{
	target_ports[ i ] = kmalloc( strlen( target_port ) + 1 + strlen( suffixes[ i ] ), GFP_KERNEL );
	if( target_ports[ i ] == NULL )
		goto die;
	strcpy( target_ports[ i ], target_port );
	strcat( target_ports[ i ], suffixes[ i ] );
	}

if( !queue_init( &rx_queue, buffer_sz ) )
	{
	goto die;
	}

if( !queue_init( &tx_queue, buffer_sz ) )
	{
	goto die;
	}

Major = register_chrdev(0, DEVICE_NAME, &our_fops);
if (Major < 0)
	{
	printk ("Registering the character device failed with %d\n", Major);
	return Major;
	}

printk("<1>I was assigned major number %d.  To talk to the driver:\n", Major);
printk("'    mknod /dev/hello c %d 0'.\n", Major);
printk("<1>Remove the device file and module when done.\n");

dev_Class = class_create( THIS_MODULE, DEVICE_NAME );
if( dev_Class == NULL )
	{
	printk( KERN_ALERT "Error!Class couldn't be created!\n" );
	goto die;
	}

for( i = 0; i < MINOR_COUNT; ++i )
	{
	chr_dev[i] = device_create( dev_Class, NULL, MKDEV(Major,i), NULL, target_ports[ i ] );
	if( chr_dev[i] == NULL )
		{
		printk( KERN_ALERT "Error!Meta Device file couldnt be created\n" );
		goto die;
		}
	}

printk(KERN_INFO "init_module() complete\n\tRealDevice:%s\n\tMetaDevice:%s\n\tRxDevice:%s\n\tTxDevice:%s\n",
	target_port,
	target_ports[ MINOR_META ],
	target_ports[ MINOR_RX ],
	target_ports[ MINOR_TX ] );

if( kern_path( target_port, LOOKUP_FOLLOW, &p ) )
	goto die;

inod = p.dentry->d_inode;
if( inod == NULL )
	{
	printk( KERN_INFO "No inode for that filename" );
	goto die;
	}


their_orig_fops = inod->i_fop;
memcpy( &their_fops, inod->i_fop, sizeof( their_fops ) );
if( their_fops.read    )their_fops.read    = target_read;
if( their_fops.write   )their_fops.write   = target_write;
if( their_fops.release )their_fops.release = target_release;
inod->i_fop = &their_fops;

printk( KERN_INFO "Orig fops:" );
print_fops( their_orig_fops );

printk( KERN_INFO "New fops:" );
print_fops( &their_fops );


return SUCCESS;

die:
	cleanup_module();
	return ERROR;
}

static void print_fops( const struct file_operations * i_fop )
{
printk( KERN_INFO "\tfops location:%x\n", i_fop );
printk( KERN_INFO "\tread:%x  write:%x\n", i_fop->read, i_fop->write );
printk( KERN_INFO "\taread:%x awrite:%x\n", i_fop->aio_read, i_fop->aio_write );
printk( KERN_INFO "\tsread:%x swrite:%x\n", i_fop->splice_read, i_fop->splice_write );
printk( KERN_INFO "\tmmap: %x\n", i_fop->mmap );
}

void cleanup_module(void)
{
int i;
struct path p;

if( !kern_path( target_port, LOOKUP_FOLLOW, &p ) )
	{
	if( p.dentry->d_inode->i_fop != &their_fops )
		{
		printk( KERN_ALERT " while unregistering, I tried to put the original fops back, but someone else has them overridden too. I'm going to put the originals back anyways, because if I don't we'll probably crash\n");
		printk( KERN_INFO "current fops:");
		print_fops( p.dentry->d_inode->i_fop );

		printk( KERN_INFO "now putting back:");
		print_fops( their_orig_fops );
		}
	p.dentry->d_inode->i_fop = their_orig_fops;
	}
else
	{
	printk( KERN_ALERT "unable to find inode for targeted device, may be leaving unstable fops!\n" );
	}

queue_destroy( &rx_queue );
queue_destroy( &tx_queue );

for( i = 0; i < MINOR_COUNT; ++i )
	{
	if( target_ports[ i ] != NULL )
		{
		kfree( target_ports[ i ] );
		}
	}

for( i = 0; i < MINOR_COUNT; ++i )
	{
	if( chr_dev[ i ] != NULL )
		{
		device_destroy ( dev_Class, MKDEV( Major, i ) );
		chr_dev[ i ] = NULL;
		}
	}

if( dev_Class != NULL )
	{
	class_destroy( dev_Class );
	dev_Class = NULL;
	}
printk(KERN_INFO "cleanup_module() called\n");
}

