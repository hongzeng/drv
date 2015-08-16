/*  sched.c - scheduale a function to be called on every timer interrupt.
 *
 *  Copyright (C) 2001 by Peter Jay Salzman
 *
 *  06/20/2006 - Updated by Rodrigo Rubira Branco <rodrigo@kernelhacking.com>
 */

/* Kernel Programming */
#define MODULE
#define LINUX
#define __KERNEL__

/* The necessary header files */

/* Standard in kernel modules */
#include <linux/kernel.h>                   /* We're doing kernel work */
#include <linux/module.h>                   /* Specifically, a module */

/* Deal with CONFIG_MODVERSIONS */
#if CONFIG_MODVERSIONS==1
#define MODVERSIONS
#include <linux/modversions.h>
#endif        

/* Necessary because we use the proc fs */
#include <linux/proc_fs.h>

/* We scheduale tasks here */
#include <linux/tqueue.h>

/* We also need the ability to put ourselves to sleep and wake up later */
#include <linux/sched.h>

/* In 2.2.3 /usr/include/linux/version.h includes a macro for this, but
 * 2.0.35 doesn't - so I add it here if necessary.
 */
#ifndef KERNEL_VERSION
#define KERNEL_VERSION(a,b,c) ((a)*65536+(b)*256+(c))
#endif

/* The number of times the timer interrupt has been called so far */
static int TimerIntrpt = 0;

/* This is used by cleanup, to prevent the module from being unloaded while
 * intrpt_routine is still in the task queue
 */
static DECLARE_WAIT_QUEUE_HEAD(WaitQ);
int waitq=0;

static void intrpt_routine(void *);

/* The task queue structure for this task, from tqueue.h */
static struct tq_struct Task = {
   routine: (void (*)(void *)) intrpt_routine, /* The function to run */
   data: NULL            /* The void* parameter for that function */
};

/* This function will be called on every timer interrupt. Notice the void*
 * pointer - task functions can be used for more than one purpose, each time 
 * getting a different parameter.
 */
static void intrpt_routine(void *irrelevant)
{
   /* Increment the counter */
   TimerIntrpt++;

   /* If cleanup wants us to die */
   if (waitq) 
      wake_up(&WaitQ);               /* Now cleanup_module can return */
   else
      /* Put ourselves back in the task queue */
      queue_task(&Task, &tq_timer);  
}

/* Put data into the proc fs file. */
int procfile_read(char *buffer, 
                  char **buffer_location, off_t offset, 
                  int buffer_length, int *eof, void *data)
{
   int len;  /* The number of bytes actually used */

   /* It's static so it will still be in memory when we leave this function
    */
   static char my_buffer[80];  

   static int count = 1;

   /* We give all of our information in one go, so if the anybody asks us
    * if we have more information the answer should always be no. 
    */
   if (offset > 0)
      return 0;

   /* Fill the buffer and get its length */
   len = sprintf(my_buffer, "Timer called %d times so far\n", TimerIntrpt);
   count++;

   /* Tell the function which called us where the buffer is */
   *buffer_location = my_buffer;

   /* Return the length */
   return len;
}

/* Proc structure pointer */
struct proc_dir_entry *Our_Proc_File;

/* Initialize the module - register the proc file */
int init_module()
{
   /* Put the task in the tq_timer task queue, so it will be executed at
    * next timer interrupt
    */
   queue_task(&Task, &tq_timer);

   /* Success if proc_register_dynamic is a success, failure otherwise */
   Our_Proc_File=create_proc_read_entry("sched", 0444, NULL, procfile_read, NULL);

   if ( Our_Proc_File == NULL )
	return -ENOMEM;
   else
	return 0;

}

/* Cleanup */
void cleanup_module()
{
   /* Unregister our /proc file */
   remove_proc_entry("sched", NULL);
  
   /* Sleep until intrpt_routine is called one last time. This is necessary,
    * because otherwise we'll deallocate the memory holding intrpt_routine
    * and Task while tq_timer still references them.  Notice that here we
    * don't allow signals to interrupt us. 
    *
    * Since WaitQ is now not NULL, this automatically tells the interrupt
    * routine it's time to die.
    */
   waitq=1;
   sleep_on(&WaitQ);
}  

MODULE_LICENSE("GPL");

