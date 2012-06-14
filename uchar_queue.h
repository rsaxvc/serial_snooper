#ifndef UCHAR_QUEUE_H
#define UCHAR_QUEUE_H

#include <linux/slab.h>

typedef struct
	{
	unsigned int write_index;
	unsigned int read_index;
	unsigned int size;
	unsigned char * data;
	}uchar_queue;

static int queue_full( const uchar_queue * q )
{
return( (q->write_index + 1 ) % q->size == q->read_index );
}

static int queue_init( uchar_queue * q, unsigned int size )
{
q->data = kmalloc( size, GFP_KERNEL );
q->size = size;
q->read_index = 0;
q->write_index = 0;
return q->data != NULL;
}

static void queue_destroy( uchar_queue * q )
{
if( q->data != NULL )
	{
	kfree( q->data );
	q->data = NULL;
	}
}

static unsigned char queue_pop( uchar_queue * q )
{
return q->data[ q->read_index++ ];
}

static void queue_push( uchar_queue * q, unsigned char b )
{
if( queue_full( q ) )
	{
	/*drop a byte*/
	queue_pop( q );
	}

q->data[ q->write_index ] = b;
q->write_index++;

if( q->write_index >= q->size )
	{
	q->write_index = 0;
	}
}

static int queue_size( const uchar_queue * q )
{
if( q->write_index >= q->read_index )
	{
	return q->write_index - q->read_index;
	}
else
	{
	return q->size - ( q->write_index - q->read_index );
	}
}

#endif
