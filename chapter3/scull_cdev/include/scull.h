#ifndef __SCULL_H_
#define __SCULL_H_
 
#include <linux/cdev.h>


#define SCULL_MAJOR   0
#define SCULL_MINOR   0 
#define SCULL_NR_DEVS 4
 
#define SCULL_QUANTUM 4000
#define SCULL_QSET 1000

 
struct scull_qset_t {
	void **data;
	struct scull_qset_t *next;
};
 
struct scull_dev {
	struct scull_qset_t *data;
	int quantum;
	int qset;
	unsigned long size;
	unsigned int access_key;
	struct semaphore sem;
	struct cdev cdev;
};
 
 
#endif
