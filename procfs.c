/*  procfs.c -  create a "file" in /proc, which allows both input and output.
 */

#include <linux/kernel.h>   /* We're doing kernel work */
#include <linux/module.h>   /* Specifically, a module */

/* Necessary because we use proc fs */
#include <linux/proc_fs.h>


/* In 2.2.3 /usr/include/linux/version.h includes a 
 * macro for this, but 2.0.35 doesn't - so I add it 
 * here if necessary. */
#ifndef KERNEL_VERSION
#define KERNEL_VERSION(a,b,c) ((a)*65536+(b)*256+(c))
#endif



#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,2,0)
#include <asm/uaccess.h>  /* for get_user and put_user */
#endif

/* The module's file functions ********************** */


/* Here we keep the last message received, to prove 
 * that we can process our input */
#define MESSAGE_LENGTH 80
static char Message[MESSAGE_LENGTH];


/* Since we use the file operations struct, we can't 
 * use the special proc output provisions - we have to 
 * use a standard read function, which is this function */
#if LINUX_VERSION_CODE &gt;= KERNEL_VERSION(2,2,0)
static ssize_t module_output(
    struct file *file,   /* The file read */
    char *buf, /* The buffer to put data to (in the
                * user segment) */
    size_t len,  /* The length of the buffer */
    loff_t *offset) /* Offset in the file - ignore */
#else
static int module_output(
    struct inode *inode, /* The inode read */
    struct file *file,   /* The file read */
    char *buf, /* The buffer to put data to (in the
                * user segment) */
    int len)  /* The length of the buffer */
#endif
{
  static int finished = 0;
  int i;
  char message[MESSAGE_LENGTH+30];

  /* We return 0 to indicate end of file, that we have 
   * no more information. Otherwise, processes will 
   * continue to read from us in an endless loop. */
  if (finished) {
    finished = 0;
    return 0;
  }

  /* We use put_user to copy the string from the kernel's 
   * memory segment to the memory segment of the process 
   * that called us. get_user, BTW, is
   * used for the reverse. */
  sprintf(message, "Last input:%s", Message);
  for(i=0; i&lt;len && message[i]; i++) 
    put_user(message[i], buf+i);


  /* Notice, we assume here that the size of the message 
   * is below len, or it will be received cut. In a real 
   * life situation, if the size of the message is less 
   * than len then we'd return len and on the second call 
   * start filling the buffer with the len+1'th byte of 
   * the message. */
  finished = 1; 

  return i;  /* Return the number of bytes "read" */
}


/* This function receives input from the user when the 
 * user writes to the /proc file. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,2,0)
static ssize_t module_input(
    struct file *file,   /* The file itself */
    const char *buf,     /* The buffer with input */
    size_t length,       /* The buffer's length */
    loff_t *offset)      /* offset to file - ignore */
#else
static int module_input(
    struct inode *inode, /* The file's inode */
    struct file *file,   /* The file itself */
    const char *buf,     /* The buffer with the input */
    int length)          /* The buffer's length */
#endif
{
  int i;

  /* Put the input into Message, where module_output 
   * will later be able to use it */
  for(i=0; i<MESSAGE_LENGTH-1 && i<length; i++)
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,2,0)
    get_user(Message[i], buf+i);
  /* In version 2.2 the semantics of get_user changed, 
   * it not longer returns a character, but expects a 
   * variable to fill up as its first argument and a 
   * user segment pointer to fill it from as the its 
   * second.
   *
   * The reason for this change is that the version 2.2 
   * get_user can also read an short or an int. The way 
   * it knows the type of the variable it should read 
   * is by using sizeof, and for that it needs the 
   * variable itself.
   */ 
#else 
    Message[i] = get_user(buf+i);
#endif
  Message[i] = '\0';  /* we want a standard, zero 
                       * terminated string */
  
  /* We need to return the number of input characters 
   * used */
  return i;
}



/* This function decides whether to allow an operation 
 * (return zero) or not allow it (return a non-zero 
 * which indicates why it is not allowed).
 *
 * The operation can be one of the following values:
 * 0 - Execute (run the "file" - meaningless in our case)
 * 2 - Write (input to the kernel module)
 * 4 - Read (output from the kernel module)
 *
 * This is the real function that checks file 
 * permissions. The permissions returned by ls -l are 
 * for referece only, and can be overridden here. 
 */
static int module_permission(struct inode *inode, int op)
{
  /* We allow everybody to read from our module, but 
   * only root (uid 0) may write to it */ 
  if (op == 4 || (op == 2 && current->euid == 0))
    return 0; 

  /* If it's anything else, access is denied */
  return -EACCES;
}




/* The file is opened - we don't really care about 
 * that, but it does mean we need to increment the 
 * module's reference count. */
int module_open(struct inode *inode, struct file *file)
{
  MOD_INC_USE_COUNT;
 
  return 0;
}


/* The file is closed - again, interesting only because 
 * of the reference count. */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,2,0)
int module_close(struct inode *inode, struct file *file)
#else
void module_close(struct inode *inode, struct file *file)
#endif
{
  MOD_DEC_USE_COUNT;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,2,0)
  return 0;  /* success */
#endif
}


/* Structures to register as the /proc file, with 
 * pointers to all the relevant functions. ********** */



/* File operations for our proc file. This is where we 
 * place pointers to all the functions called when 
 * somebody tries to do something to our file. NULL 
 * means we don't want to deal with something. */
static struct file_operations File_Ops_4_Our_Proc_File =
  {
    NULL,  /* lseek */
    module_output,  /* "read" from the file */
    module_input,   /* "write" to the file */
    NULL,  /* readdir */
    NULL,  /* select */
    NULL,  /* ioctl */
    NULL,  /* mmap */
    module_open,    /* Somebody opened the file */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,2,0)
    NULL,   /* flush, added here in version 2.2 */
#endif
    module_close,    /* Somebody closed the file */
    /* etc. etc. etc. (they are all given in 
     * /usr/include/linux/fs.h). Since we don't put 
     * anything here, the system will keep the default
     * data, which in Unix is zeros (NULLs when taken as 
     * pointers). */
  };



/* Inode operations for our proc file. We need it so 
 * we'll have some place to specify the file operations 
 * structure we want to use, and the function we use for 
 * permissions. It's also possible to specify functions 
 * to be called for anything else which could be done to 
 * an inode (although we don't bother, we just put 
 * NULL). */
static struct inode_operations Inode_Ops_4_Our_Proc_File =
  {
    &File_Ops_4_Our_Proc_File,
    NULL, /* create */
    NULL, /* lookup */
    NULL, /* link */
    NULL, /* unlink */
    NULL, /* symlink */
    NULL, /* mkdir */
    NULL, /* rmdir */
    NULL, /* mknod */
    NULL, /* rename */
    NULL, /* readlink */
    NULL, /* follow_link */
    NULL, /* readpage */
    NULL, /* writepage */
    NULL, /* bmap */
    NULL, /* truncate */
    module_permission /* check for permissions */
  };


/* Directory entry */
static struct proc_dir_entry Our_Proc_File = 
  {
    0, /* Inode number - ignore, it will be filled by 
        * proc_register[_dynamic] */
    7, /* Length of the file name */
    "rw_test", /* The file name */
    S_IFREG | S_IRUGO | S_IWUSR, 
    /* File mode - this is a regular file which 
     * can be read by its owner, its group, and everybody
     * else. Also, its owner can write to it.
     *
     * Actually, this field is just for reference, it's
     * module_permission that does the actual check. It 
     * could use this field, but in our implementation it
     * doesn't, for simplicity. */
    1,  /* Number of links (directories where the 
         * file is referenced) */
    0, 0,  /* The uid and gid for the file - 
            * we give it to root */
    80, /* The size of the file reported by ls. */
    &Inode_Ops_4_Our_Proc_File, 
    /* A pointer to the inode structure for
     * the file, if we need it. In our case we
     * do, because we need a write function. */
    NULL  
    /* The read function for the file. Irrelevant, 
     * because we put it in the inode structure above */
  }; 



/* Module initialization and cleanup ******************* */

/* Initialize the module - register the proc file */
int init_module()
{
  /* Success if proc_register[_dynamic] is a success, 
   * failure otherwise */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,2,0)
  /* In version 2.2, proc_register assign a dynamic 
   * inode number automatically if it is zero in the 
   * structure , so there's no more need for 
   * proc_register_dynamic
   */
  return proc_register(&proc_root, &Our_Proc_File);
#else
  return proc_register_dynamic(&proc_root, &Our_Proc_File);
#endif
}


/* Cleanup - unregister our file from /proc */
void cleanup_module()
{
  proc_unregister(&proc_root, Our_Proc_File.low_ino);
}  

