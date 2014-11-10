/***************************************************************************************************
 * Author : Rishi Gupta
 * Email  : gupt21@gmail.com
 *
 * This file is part of 'serial communication manager' program.
 *
 * 'serial communication manager' is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * 'serial communication manager' is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with serial communication manager. If not, see <http://www.gnu.org/licenses/>.
 *
 ***************************************************************************************************/

/* Native code to communicate with tty-style port in Unix-like operating systems
 * These references have been used.
 *
 * For Linux:
 * <KERNEL_SOURCE>/drivers/tty/serial
 * <KERNEL_SOURCE>/include/linux/serial.h, serial_8250.h, serial_core.h
 * <KERNEL_SOURCE>/drivers/usb/serial
 *
 * For Mac OS
 *
 *
 * For Solaris:
 * http://docs.oracle.com/cd/E36784_01/html/E36884/termio-7i.html#REFMAN7termio-7i
 * http://docs.oracle.com/cd/E36784_01/html/E36873/termios.h-3head.html
 * http://docs.oracle.com/cd/E36784_01/html/E36874/termios-3c.html
 * http://docs.oracle.com/cd/E36784_01/html/E36884/termiox-7i.html */

#if defined (__linux__) || defined (__APPLE__) || defined (__SunOS)

/*
 * There will be only one instance of this shared library at runtime. So if something goes wrong
 * it will affect everything, until this library has been unloaded and then loaded again.
 *
 * When printing error number, number returned by OS is printed as it is.
 */

/* Make primitives such as read and write resume, in case they are interrupted by signal,
 * before they actually start reading or writing data. The partial success case are handled
 * at appropriate places in functions applicable.
 * For details see features.h about MACROS defined below. */
#define _BSD_SOURCE || _GNU_SOURCE

/* Common interface with java layer for supported OS types. */
#include "com_embeddedunveiled_serial_SerialComJNINativeInterface.h"

#include <jni.h>

#include <unistd.h>      /* UNIX standard function definitions  */
#include <stdarg.h>      /* ISO C Standard. Variable arguments  */
#include <stdio.h>       /* ISO C99 Standard: Input/output      */
#include <stdlib.h>      /* Standard ANSI routines              */
#include <string.h>      /* String function definitions         */
#include <fcntl.h>       /* File control definitions            */
#include <errno.h>       /* Error number definitions            */
#include <dirent.h>      /* Format of directory entries         */
#include <sys/types.h>   /* Primitive System Data Types         */
#include <sys/stat.h>    /* Defines the structure of the data   */
#include <pthread.h>     /* POSIX thread definitions            */

#include "unix_like_serial_lib.h"

#ifdef __linux__
#include <linux/types.h>
#include <linux/termios.h>  /* POSIX terminal control definitions for Linux (termios2) */
#include <linux/serial.h>
#include <linux/ioctl.h>
#include <sys/eventfd.h>    /* Linux eventfd for event notification. */
#endif

#ifdef __APPLE__
#include <termios.h>
#include <sys/ioctl.h>
#include <paths.h>
#include <sysexits.h>
#include <sys/param.h>
#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <IOKit/serial/IOSerialKeys.h>
#include <IOKit/serial/ioss.h>
#include <IOKit/IOBSD.h>
#endif

#ifdef __SunOS
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/filio.h>
#endif

#undef  UART_NATIVE_LIB_VERSION
#define UART_NATIVE_LIB_VERSION "1.0.0"

/* This is the maximum number of threads and hence data listeners instance we support. */
#define MAX_NUM_THREADS 1024

/* Reference to JVM shared among all the threads. */
JavaVM *jvm;

/* When creating data looper threads, we pass some data to thread. A index in this array, holds pointer to
 * the structure which is passed as parameter to a thread. Every time a data looper thread is created, we
 * save the location of parameters passed to it and update the index to be used next time.
 *
 * This array is protected by mutex locks.
 * Functions creating data/event threads write/modify data in this array.
 * Functions destroying data/event thread delete/modify data in this array.
 * Functions in thread or the thread itself only read data in this array. */
int dtp_index = 0;
struct com_thread_params fd_looper_info[MAX_NUM_THREADS] = { {0} };

/* Used to protect global data from concurrent access. */
pthread_mutex_t mutex;

/* For Solaris, we maintain an array which will list all ports that have been opened. Now if somebody tries to open already
 * opened port claiming to be exclusive owner, we will deny the request, except for root user. */
#ifdef __SunOS
struct port_name_owner opened_ports_list[MAX_NUM_THREADS] = { {0} };
#endif

/*
 * Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
 * Method:    initNativeLib
 * Signature: ()I
 *
 * This function save reference to JVM which will be used across native library, threads etc. It prepares mutex
 * object also ready for use.
 */
JNIEXPORT jint JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_initNativeLib(JNIEnv *env, jobject obj) {
	int ret = 0;
	int negative = -1;

	ret = (*env)->GetJavaVM(env, &jvm);
	if(ret < 0) {
		fprintf(stderr, "%s \n", "NATIVE initNativeLib() could not get JVM.");
		return -240;
	}

	errno = 0;
	ret = pthread_mutex_init(&mutex, NULL);
	if(ret < 0) {
		fprintf(stderr, "%s %d\n", "NATIVE initNativeLib() failed to init mutex with error number : -", errno);
		return (negative * errno);
	}

	return 0;
}

/*
 * Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
 * Method:    getNativeLibraryVersion
 * Signature: ()Ljava/lang/String;
 *
 * This might return null which is handled by java layer.
 */
JNIEXPORT jstring JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_getNativeLibraryVersion (JNIEnv *env, jobject obj) {
	jstring version = (*env)->NewStringUTF(env, UART_NATIVE_LIB_VERSION);
	if((*env)->ExceptionOccurred(env)) {
		LOGE(env);
	}
	return version;
}

/*
 * Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
 * Method:    getSerialPortNames
 * Signature: ()[Ljava/lang/String;
 *
 * Check if the entry in /sys/class/tty has a driver associated with it. If it has we assume it is a valid serial port.
 * For example for a USB-SERIAL converter, we can verify this from shell by executing readlink command on path:
 * $ readlink /sys/class/tty/ttyUSB0/device/driver
 * ../../../../../../../bus/usb-serial/drivers/pl2303
 *
 * We don't try to open as on some bluetooth device, this results in system trying to make BT connection and failing with time out.
 * We have assumed system will not have more than 100 ports. Further, we used OS specific way to identify available serial port.
 *
 * For Solaris, this is handled in java layer itself.
 */
JNIEXPORT jobjectArray JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_getSerialPortNames(JNIEnv *env, jobject obj) {

#if defined (__linux__)
	int i=0;
	int ret = -1;
	int num_of_dir_found = 0;
	char path[1024];
	char buffer[1024];
	char* sysfspath = "/sys/class/tty/";
	const char nulll[1] = "\0";
	struct dirent **namelist;
	struct stat statbuf = {0};
	char *ports_identified[100];
	int portsadded = 0;

	num_of_dir_found = scandir(sysfspath, &namelist, NULL, NULL);
	if(num_of_dir_found >= 0) {
		while(num_of_dir_found--) {
			memset(path, 0, sizeof(path));
			if (strcmp(namelist[num_of_dir_found]->d_name, "..") && strcmp(namelist[num_of_dir_found]->d_name, ".")) {
				strcpy(path, sysfspath);
				strcat(path, namelist[num_of_dir_found]->d_name);
				strcat(path, "/device");
				strcat(path, nulll);

				ret = lstat(path, &statbuf);
			    if (ret >= 0) {
			    	if(S_ISLNK(statbuf.st_mode)) {
			    		memset(buffer, 0, sizeof(buffer));
			    		strncpy(path, path, strlen(path));
			    		strcat(path, "/driver");
			    		strcat(path, nulll);

			    		ret = readlink(path, buffer, sizeof(buffer));
			    		if(ret >= 0) {
			    			if(strlen(buffer) > 0) {
			    				ports_identified[portsadded] = namelist[num_of_dir_found]->d_name;
			    				portsadded++;
			    			}
			    		} else {
			    			/* fprintf(stderr, "%s \n", "ERROR in readlink the file."); un-comment only when debugging code. */
			    		}
			    	}
			    } else {
			    	/* fprintf(stderr, "%s \n", "ERROR in stat directory."); un-comment only when debugging code. */
			    }

			}
			free(namelist[num_of_dir_found]);
		}
		free(namelist);

		/* Prepare full path to device node as per Linux standard.
		 * Create a JAVA/JNI style array of String object, populate it and return to java layer. */
		char *devnode;
		char devbase[100];

		jclass strClass = (*env)->FindClass(env, "java/lang/String");
		if( (*env)->ExceptionOccurred(env) ) {
			LOGE(env);
		}

		jobjectArray portsFound = (*env)->NewObjectArray(env, (jsize)portsadded, strClass, NULL);
		if( (*env)->ExceptionOccurred(env) ) {
			LOGE(env);
		}

		for(i=0; i<portsadded; i++) {
			devnode = strcat( strcpy(devbase, "/dev/"), ports_identified[i]);
			(*env)->SetObjectArrayElement(env, portsFound, i, (*env)->NewStringUTF(env, devnode));
			if( (*env)->ExceptionOccurred(env) ) {
				LOGE(env);
			}
		}
		return portsFound;

	} else {
		fprintf(stderr, "%s \n", "ERROR scanning directory : /sys/class/tty/");
	}
	return NULL;
#endif

#if defined (__APPLE__)
	CFMutableDictionaryRef matchingDict;
	io_iterator_t iter = 0;
	io_service_t service = 0;
	kern_return_t kr;
	CFStringRef cfCalloutPath;
	Char calloutPath[512];
	int i = 0;
	char *ports_identified[100];
	int portsadded = 0;
	memset(calloutPath, 0, sizeof(calloutPath));

	/* Create a matching dictionary that will find any serial device. */
	matchingDict = IOServiceMatching(kIOSerialBSDServiceValue);
	kr = IOServiceGetMatchingServices(kIOMasterPortDefault, matchingDict, &iter);
	if (kr != KERN_SUCCESS) {
		fprintf(stderr, "%s %ld \n", "NATIVE getSerialPortNames() failed in IOServiceGetMatchingServices() with error", kr);
		return -240;
	}

	/* Iterate over all matching objects. */
	while((service = IOIteratorNext(iter)) != 0) {
		/* Get the character device path. */
		cfCalloutPath = IORegistryEntryCreateCFProperty(service, CFSTR(kIOCalloutDeviceKey), kCFAllocatorDefault, 0);
		CFStringGetCString(cfCalloutPath, calloutPath, sizeof(calloutPath), kCFStringEncodingUTF8);
		CFRelease(cfCalloutPath);
		IOObjectRelease(service); /* The I/O Registry object is no longer needed. */

		/* Save the found port. */
		fprintf(stderr, "Found device %s at path %s \n", deviceName, calloutPath);
		ports_identified[portsadded] = calloutPath;
		portsadded++;
	}
	IOObjectRelease(iter); /* Release iterator. */

	/* Prepare the array, populate it and return to java layer. */
	jclass strClass;
	jobjectArray portsFound;

	strClass = (*env)->FindClass(env, "java/lang/String");
	if((*env)->ExceptionOccurred(env)) {
		LOGE(env);
	}
	portsFound = (*env)->NewObjectArray(env, (jsize)portsadded, strClass, NULL);
	if((*env)->ExceptionOccurred(env)) {
		LOGE(env);
	}
	for(i=0; i<portsadded; i++) {
		(*env)->SetObjectArrayElement(env, portsFound, i, (*env)->NewStringUTF(env, ports_identified[i]));
		if( (*env)->ExceptionOccurred(env) ) {
			LOGE(env);
		}
	}
	return portsFound;

#endif
}

/*
 * Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
 * Method:    openComPort
 * Signature: (Ljava/lang/String;ZZZ)J
 *
 * Open and initialise the port because 'termios' settings persist even if port has been closed.
 * We set default settings as; non-canonical mode, 9600 8N1 with no time out and no delay.
 * The terminal settings set here, are to operate in raw-like mode (no characters interpreted).
 * Note that all the bit mask are defined using OCTAL representation of number system.
 *
 */
JNIEXPORT jlong JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_openComPort(JNIEnv *env, jobject obj, jstring portName, jboolean enableRead, jboolean enableWrite, jboolean exclusiveOwner) {
	jint ret = -1;
	jlong fd = -1;
	jint negative = -1;
	jint OPEN_MODE = -1;

#if defined (__linux__)
	struct termios2 settings = {0};
#elif defined (__APPLE__) || defined (__SunOS)
	struct termios settings = {0};
#endif

	memset(&settings, 0, sizeof(settings));
	const char* portpath = (*env)->GetStringUTFChars(env, portName, NULL);
	if(portpath == NULL) {
		fprintf(stderr, "%s \n", "NATIVE openComPort() failed to create portpath string.");
		return -240;
	}

	if( (enableRead == JNI_TRUE) && (enableWrite == JNI_TRUE) ) {
		OPEN_MODE = O_RDWR;
	} else if (enableRead == JNI_TRUE) {
		OPEN_MODE = O_RDONLY;
	} else if (enableWrite == JNI_TRUE) {
		OPEN_MODE = O_WRONLY;
	}

	errno = 0;
	fd = open(portpath, OPEN_MODE | O_NOCTTY | O_NONBLOCK);
	if(fd < 0) {
		fprintf(stderr, "%s %d\n", "openComPort() failed to open requested port with error number : -", errno);
		(*env)->ReleaseStringUTFChars(env, portName, portpath);
		return (negative * errno);
	}
	(*env)->ReleaseStringUTFChars(env, portName, portpath);

	/* Make the caller, exclusive owner of this port. This will prevent additional opens except by root-owned process. */
	if(exclusiveOwner == JNI_TRUE) {
#if defined (__linux__) || defined (__APPLE__)
		errno = 0;
		ret = ioctl(fd, TIOCEXCL);
		if(ret < 0) {
			fprintf(stderr, "%s %d\n", "NATIVE openComPort() failed to become exclusive owner of port with error number : -", errno);
			return (negative * errno);
		}
#elif defined (__SunOS)
		fprintf(stderr, "%s %d\n", "NATIVE openComPort(). Exclusive ownership is not supported for Solaris as of now.");
		return -1;
#endif
	}

	/* Control options :
	 * CREAD and CLOCAL are enabled to make sure that the caller program does not become the owner of the
	 * port subject to sporatic job control and hang-up signals, and also that the serial interface driver
	 * will read incoming bytes. CLOCAL results in ignoring modem status lines while CREAD enables receiving
	 * data. CRTSCTS indicates no hardware flow control. */
	settings.c_cflag &= ~CRTSCTS;                                   /* Not is POSIX, requires _BSD_SOURCE */
	settings.c_cflag &= ~CSIZE;
	settings.c_cflag &= ~PARENB;
	settings.c_cflag &= ~CSTOPB;
	settings.c_cflag |= (CS8 | CREAD | CLOCAL);

	/* Control characters :
	 * Return immediately if no data is available on read() call and no time out value. */
	settings.c_cc[VMIN] = 0;
	settings.c_cc[VTIME] = 0;

	/* Input options :
	 * IGNBRK : Ignore break conditions, INLCR : Don't Map NL to CR, ICRNL : Don't Map CR to NL */
	settings.c_iflag &= ~(IMAXBEL | IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);

	/* Output options :
	 * OPOST : No output processing, ONLCR : Don't convert line feeds */
	settings.c_oflag &= ~OPOST;
	settings.c_oflag &= ~ONLCR;

	/* Line options :
	 * Non-canonical mode is enabled. Do not echo. */
	settings.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);

	/* Line discipline : */
	settings.c_line = 0;

	/* Set 9600 baud per second as default baud rate and apply settings to specified tty port. */
#if defined (__linux__)
	settings.c_ispeed = B9600;
	settings.c_ospeed = B9600;

	errno = 0;
	ret = ioctl(fd, TCSETS2, &settings);
	if(ret < 0) {
		fprintf(stderr, "%s %d\n", "NATIVE openComPort() failed to set default terminal settings with error number : -", errno);
		return (negative * errno);
	}

	/* Clear port IO buffers. */
	ioctl(fd, TCFLSH, TCIOFLUSH);

#elif defined (__APPLE__) || defined (__SunOS)
	errno = 0;
	ret = cfsetspeed(&settings, B9600);
	if(ret < 0) {
		fprintf(stderr, "%s %d\n", "NATIVE openComPort() failed to set default baud rate setting with error number : -", errno);
		return (negative * errno);
	}

	errno = 0;
	ret  = tcsetattr(fd, TCSANOW, &settings);
	if(ret < 0) {
		fprintf(stderr, "%s %d\n", "NATIVE openComPort() failed to set default terminal settings with error number : -", errno);
		return (negative * errno);
	}

	tcflush(fd, TCIOFLUSH);

#endif

	return fd;
}

/*
 * Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
 * Method:    closeComPort
 * Signature: (J)I
 *
 * Free the file descriptor for reuse and tell kernel to free up structures associated with this file. The close system call
 * does not flush any data in Linux, so caller should make sure that he has taken care of this. The close system call return
 * 0 on success.
 */
JNIEXPORT jint JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_closeComPort(JNIEnv *env, jobject obj, jlong fd) {
	jint ret = -1;
	jint negative = -1;

	/* int x = 0;
	struct com_thread_params *ptr;
	ptr = fd_looper_info; */

	/* Flush all remaining data if any to the receiver. */
#if defined (__linux__)
	ret = ioctl(fd, TCSBRK, 1);
#elif defined (__APPLE__) || defined (__SunOS)
	ret = tcdrain(fd);
#endif

	if(ret < 0) {
		fprintf(stderr, "%s %d\n", "Native closeComPort() failed to flush data to receiver with error number : -", errno);
		fprintf(stderr, "%s\n", "Still proceeding to close port.");
	}

	/* Failing disclaiming exclusive ownership of port will produce unexpected results if same port is to be used by more users.
	 * So if we fail we return with error and user application should retry closing. */
#if defined (__linux__) || defined (__APPLE__)
	errno = 0;
	ret = ioctl(fd, TIOCNXCL);
	if(ret < 0) {
		fprintf(stderr, "%s %d\n", "Native closeComPort() failed to release exclusive ownership of port with error number : -", errno);
		return (negative * errno);
	}
#endif

	/* Whether we were able to flush remaining data or not, we proceed to close port. */
	errno = 0;
	ret = close(fd);
	if(ret < 0) {
		fprintf(stderr, "%s %d\n", "Native closeComPort() failed to close port with error number : -", errno);
		return (negative * errno);
	}

	/* More analysis required http://manpages.ubuntu.com/manpages/precise/man7/epoll.7.html
	pthread_mutex_lock(&mutex);
	// Update the entry for this fd in global array.
	for(x=0; x<MAX_NUM_THREADS; x++) {
		if(ptr->fd == fd) {
			ptr->fd = -1;
			break;
		}
		ptr++;
	}
	pthread_mutex_unlock(&mutex); */

	return ret;
}

/*
 * Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
 * Method:    readBytes
 * Signature: (JI)[B
 *
 * The maximum number of bytes that read system call can read is the value that can be stored in an object of type ssize_t.
 * In JNI programming 'jbyte' is 'signed char'. Default count is set to 1024 in java layer.
 */
JNIEXPORT jbyteArray JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_readBytes(JNIEnv *env, jobject obj, jlong fd, jint count) {
	int i = -1;
	int index = 0;
	int partialData = -1;
	ssize_t ret = -1;
	jbyte buffer[count];
	jbyte final_buf[count * 3]; /* Sufficient enough to deal with consecutive multiple partial reads. */
	jbyte empty_buf[] = { };
	jbyteArray dataRead;

	do {
		errno = 0;
		ret = read(fd, buffer, sizeof(buffer));
		if(ret > 0 && errno == 0) {
			/* This indicates we got success and have read data. */
			/* If there is partial data read previously, append this data. */
			if(partialData == 1) {
				for(i = index; i < ret; i++) {
					final_buf[i] = buffer[i];
				}
				dataRead = (*env)->NewByteArray(env, index + ret);
				(*env)->SetByteArrayRegion(env, dataRead, 0, index + ret, final_buf);
				return dataRead;
			} else {
				/* Pass the successful read to java layer straight away. */
				dataRead = (*env)->NewByteArray(env, ret);
				(*env)->SetByteArrayRegion(env, dataRead, 0, ret, buffer);
				return dataRead;
			}
		} else if (ret > 0 && errno == EINTR) {
			/* This indicates, there is data to read, however, we got interrupted before we finish reading
			 * all of the available data. So we need to save this partial data and get back to read remaining. */
			for(i = index; i < ret; i++) {
				final_buf[i] = buffer[i];
			}
			index = ret;
			partialData = 1;
			continue;
		} else if(ret < 0) {
			if(errno == EAGAIN || errno == EWOULDBLOCK) {
				/* This indicates, there was no data to read. Therefore just return null. */
				dataRead = (*env)->NewByteArray(env, sizeof(empty_buf));
				(*env)->SetByteArrayRegion(env, dataRead, 0, sizeof(empty_buf), empty_buf);
				return dataRead;
			} else if(errno != EINTR) {
				/* This indicates, irrespective of, there was data to read or not, we got an error during operation. */
				/* Can we handle this condition more gracefully. */
				fprintf(stderr, "%s %d\n", "Native readBytes() failed to read data with error number : -", errno);
				break;
			} else if (errno == EINTR) {
				/* This indicates that we should retry as we are just interrupted by a signal. */
				continue;
			}
		} else if (ret == 0) {
			/* This indicates, there was no data to read or EOF. */
			dataRead = (*env)->NewByteArray(env, sizeof(empty_buf));
			(*env)->SetByteArrayRegion(env, dataRead, 0, sizeof(empty_buf), empty_buf);
			return dataRead;
		}
	} while(1);

	return NULL;
}

/*
 * Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
 * Method:    writeBytes
 * Signature: (J[BI)I
 *
 * Note that write method return success does not mean data has been sent to receiver. Therefore we flush data after writing using 'TCSBRK' ioctl.
 * Delay is in micro-seconds.
 */
JNIEXPORT jint JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_writeBytes(JNIEnv *env, jobject obj, jlong fd, jbyteArray buffer, jint delay) {
	jint ret = -1;
	jint negative = -1;

    jbyte* data_buf = (*env)->GetByteArrayElements(env, buffer, JNI_FALSE);
    size_t count = (size_t) (*env)->GetArrayLength(env, buffer);

    errno = 0;
    if(delay == 0) {
		ret = write(fd, data_buf, count);
		if(ret < 0) {
			fprintf(stderr, "%s %d\n", "NATIVE writeBytes() failed to write requested data with error number : -", errno);
			return (negative * errno);
		}
    } else {
		ret = write(fd, data_buf, count);
		if(ret < 0) {
			fprintf(stderr, "%s %d\n", "NATIVE writeBytes() failed to write requested data with error number : -", errno);
			return (negative * errno);
		}
    }

    (*env)->ReleaseByteArrayElements(env, buffer, data_buf, 0);

    ioctl(fd, TCSBRK, 1);
    return 0;
}

/*
 * Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
 * Method:    configureComPortData
 * Signature: (JIIIII)I
 */
JNIEXPORT jint JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_configureComPortData(JNIEnv *env, jobject obj, jlong fd, jint dataBits, jint stopBits, jint parity, jint baudRateTranslated, jint custBaudTranslated) {
	jint ret = 0;
	jint negative = -1;

#if defined (__linux__)
	struct termios2 currentconfig = {0};
	errno = 0;
	ret = ioctl(fd, TCGETS2, &currentconfig);
	if(ret < 0) {
		fprintf(stderr, "%s %d\n", "NATIVE configureComPortData() failed to get current configuration with error number : -", errno);
		fprintf(stderr, "%s\n", "Please try again");
		return (negative * errno);
	}
#elif defined (__APPLE__) || defined (__SunOS)
	struct termios currentconfig = {0};
	errno = 0;
	ret = tcgetattr(fd, &currentconfig);
	if(ret < 0) {
		fprintf(stderr, "%s %d\n", "NATIVE configureComPortData() failed to get current configuration with error number : -", errno);
		fprintf(stderr, "%s\n", "Please try again");
		return (negative * errno);
	}
#endif

	/* We handle custom baud rate case first so as to make development/debugging easy for developers. */
	if(baudRateTranslated == 251) {
#if defined (__linux__)
		currentconfig.c_ispeed = custBaudTranslated;
		currentconfig.c_ospeed = custBaudTranslated;

		errno = 0;
		ret = ioctl(fd, TCSETS2, &currentconfig);
		if(ret < 0) {
			fprintf(stderr, "%s %d\n", "NATIVE configureComPortData() failed to set desired baud rate with error number : -", errno);
			return (negative * errno);
		}

#elif defined (__APPLE__)
		speed_t speed = custBaudTranslated;
		errno = 0;
		ret = ioctl(fd, IOSSIOSPEED, &speed);
		if(ret < 0) {
			fprintf(stderr, "%s %d\n", "NATIVE configureComPortData() failed to set desired baud rate with error number : -", errno);
			return (negative * errno);
		}

#elif defined (__SunOS)
		/* Solaris does not support custom baud rates. */
		fprintf(stderr, "%s\n", "This baud rate is not supported by OS platform.");
#endif

	} else {
		/* Handle standard baud rate setting. */
		int baud = -1;
		switch(baudRateTranslated) {
					case 1: baud = B0;
						break;
					case 2: baud = B50;
						break;
					case 3: baud = B75;
						break;
					case 4: baud = B110;
						break;
					case 5: baud = B134;
						break;
					case 6: baud = B150;
						break;
					case 7: baud = B200;
						break;
					case 8: baud = B300;
						break;
					case 9: baud = B600;
						break;
					case 10: baud = B1200;
						break;
					case 11: baud = B1800;
						break;
					case 12: baud = B2400;
						break;
					case 13: baud = B4800;
						break;
					case 14: baud = B9600;
						break;
					case 15: baud = B19200;
						break;
					case 16: baud = B38400;
						break;
					case 17: baud = B57600;
						break;
					case 18: baud = B115200;
						break;
					case 19: baud = B230400;
						break;
					case 20: baud = B460800;
						break;
					case 21: baud = B500000;
						break;
					case 22: baud = B576000;
						break;
					case 23: baud = B921600;
						break;
					case 24: baud = B1000000;
						break;
					case 25: baud = B1152000;
						break;
					case 26: baud = B1500000;
						break;
					case 27: baud = B2000000;
						break;
					case 28: baud = B2500000;
						break;
					case 29: baud = B3000000;
						break;
					case 30: baud = B3500000;
						break;
					case 31: baud = B4000000;
						break;
					default : baud = -1;
						break;
				}
#if defined (__linux__)
		currentconfig.c_ispeed = baud;
		currentconfig.c_ospeed = baud;
#elif defined (__APPLE__) || defined (__SunOS)
		errno = 0;
		ret = cfsetspeed(&currentconfig, baud);
		if(ret < 0) {
			fprintf(stderr, "%s %d\n", "NATIVE configureComPortData() failed to set desired terminal settings with error number : -", errno);
			return (negative * errno);
		}
#endif
	}

	/* Set data bits. */
	currentconfig.c_cflag &= ~CSIZE;
	switch(dataBits) {
		case 5:
			currentconfig.c_cflag |= CS5;
			break;
		case 6:
			currentconfig.c_cflag |= CS6;
			break;
		case 7:
			currentconfig.c_cflag |= CS7;
			break;
		case 8:
			currentconfig.c_cflag |= CS8;
			break;
	}

	/* Set stop bits. If CSTOPB is not set one stop bit is used. Otherwise two stop bits are used. */
	if(stopBits == 1) {
		currentconfig.c_cflag &= ~CSTOPB; /* one stop bit used if user set 1 stop bit */
	} else {
		currentconfig.c_cflag |= CSTOPB;  /* two stop bits used if user set 1.5 or 2 stop bits */
	}

	/* Clear existing parity and then set new parity.
	 * INPCK  : Enable checking parity of data.
	 * ISTRIP : Strip parity bit from data before sending it to application.
	 * CMSPAR : Mark or space (stick) parity (Linux OS). Not is POSIX, requires _BSD_SOURCE.
	 * PAREXT : Extended parity for mark and space parity (AIX OS).  */
#if defined(CMSPAR)
		currentconfig.c_cflag &= ~(PARENB | PARODD | CMSPAR);
#elif defined(PAREXT)
		currentconfig.c_cflag &= ~(PARENB | PARODD | PAREXT);
#else
		currentconfig.c_cflag &= ~(PARENB | PARODD);
#endif

	switch(parity) {
		case 1:
			currentconfig.c_cflag &= ~PARENB;                    /* No parity */
			break;
		case 2:
			currentconfig.c_cflag |= (PARENB | PARODD);          /* Odd parity */
			currentconfig.c_iflag |= (INPCK);
			break;
		case 3:
			currentconfig.c_cflag |= PARENB;                     /* Even parity */
			currentconfig.c_cflag &= ~PARODD;
			currentconfig.c_iflag |= (INPCK);
			break;
		case 4:
#if defined(CMSPAR)
			currentconfig.c_cflag |= (PARENB | PARODD | CMSPAR); /* Mark parity */
#elif defined(PAREXT)
			currentconfig.c_cflag |= (PARENB | PARODD | PAREXT);
#endif
			currentconfig.c_iflag |= (INPCK);
			break;
		case 5:
#if defined(CMSPAR)
			currentconfig.c_cflag |= (PARENB | CMSPAR);          /* Space parity */
#elif defined(PAREXT)
			currentconfig.c_cflag |= (PARENB | PAREXT);
#endif
			currentconfig.c_cflag &= ~PARODD;
			currentconfig.c_iflag |= (INPCK);
			break;
	}

	/* Apply changes/settings to the termios associated with this port. */
#if defined (__linux__)
	ioctl(fd, TCSETS2, &currentconfig);

#elif defined (__APPLE__) || defined (__SunOS)
	errno = 0;
	ret  = tcsetattr(fd, TCSANOW, &currentconfig);
	if(ret < 0) {
		fprintf(stderr, "%s %d\n", "NATIVE configureComPortData() failed to set desired terminal settings with error number : -", errno);
		return (negative * errno);
	}
#endif

	/* Clear IO buffers after applying new valid settings to make port in 100% sane condition. */
#if defined (__linux__)
	ioctl(fd, TCFLSH, TCIOFLUSH);
	ioctl(fd, TCFLSH, TCIOFLUSH);
#elif defined (__APPLE__) || defined (__SunOS)
	tcflush(fd, TCIOFLUSH);
	tcflush(fd, TCIOFLUSH);
#endif

	return 0;
}

/*
 * Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
 * Method:    configureComPortControl
 * Signature: (JICCZZ)I
 *
 * For software flow control; IXON, IXOFF, and IXANY are used . If IXOFF is set, then software flow control is enabled on
 * the TTY's input queue. The TTY transmits a STOP character when the program cannot keep up with its input queue and transmits a START
 * character when its input queue in nearly empty again. If IXON is set, software flow control is enabled on the TTY's output queue. The
 * TTY blocks writes by the program when the device to which it is connected cannot keep up with it. If IXANY is set, then any character
 * received by the TTY from the device restarts the output that has been suspended.
 */
JNIEXPORT jint JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_configureComPortControl(JNIEnv *env, jobject obj, jlong fd, jint flowctrl, jchar xon, jchar xoff, jboolean ParFraError, jboolean overFlowErr) {

	jint ret = 0;
	jint negative = -1;

#if defined (__linux__)
	struct termios2 currentconfig = {0};
	errno = 0;
	ret = ioctl(fd, TCGETS2, &currentconfig);
	if(ret < 0) {
		fprintf(stderr, "%s %d\n", "NATIVE configureComPortControl() failed to get current configuration with error number : -", errno);
		fprintf(stderr, "%s\n", "Please try again");
		return (negative * errno);
	}
#elif defined (__APPLE__) || defined (__SunOS)
	struct termios currentconfig = {0};
	errno = 0;
	ret = tcgetattr(fd, &currentconfig);
	if(ret < 0) {
		fprintf(stderr, "%s %d\n", "NATIVE configureComPortControl() failed to get current configuration with error number : -", errno);
		fprintf(stderr, "%s\n", "Please try again");
		return (negative * errno);
	}
#endif

	/* Set flow control. */
	if(flowctrl == 1) {
		currentconfig.c_cflag &= ~CRTSCTS;                  /* No flow control. */
	} else if(flowctrl == 2) {                             /* Hardware flow control. */
		currentconfig.c_iflag &= ~(IXON | IXOFF | IXANY);
		currentconfig.c_cflag &= HUPCL;                     /* Disconnect the line when the last process closes the device. Infers drop DTR. */
		currentconfig.c_cflag |= CRTSCTS;                   /* Specifying hardware flow control. */
		currentconfig.c_cflag &= ~CLOCAL;
	} else if(flowctrl == 3) {
		currentconfig.c_iflag |= (IXON | IXOFF | IXANY);    /* Software flow control on both tx and rx data. */
		currentconfig.c_cc[VSTART] = xon;                   /* The value of the XON character for both transmission and reception. */
		currentconfig.c_cc[VSTOP] = xoff;                   /* The value of the XOFF character for both transmission and reception. */
	}

	/* Set parity and frame error. */
	if(ParFraError == JNI_TRUE) {
		/* First check if user has enabled parity checking or not. */
		if( ! ((currentconfig.c_cflag & PARENB) == PARENB) ) {
			fprintf(stderr, "%s\n", "Parity checking is not enabled first via configureComPortData method");
			return -1;
		}

		/* Mark the character as containing an error. This will cause a character containing a parity or framing error to be
		 * replaced by a three character sequence consisting of the erroneous character preceded by \377 and \000. A legitimate
		 * received \377 will be replaced by a pair of \377s.*/
		currentconfig.c_iflag |= (PARMRK);
		currentconfig.c_iflag &= ~IGNPAR;
	} else {
		/* Ignore the character containing an error. Any received characters containing parity errors will be silently dropped. */
		currentconfig.c_iflag |= (IGNPAR);
		currentconfig.c_iflag &= ~PARMRK;
	}

	/* Set buffer overrun error.
	 * Echo the ASCII BEL character, 0x07 or as defined in 'c_cc' when the input stream overflows.
	 * Additional data is lost.  If MAXBEL is not set, the BEL character is not sent but the data is lost anyhow. */
	if(overFlowErr == JNI_TRUE) {
		currentconfig.c_iflag |= IMAXBEL;
	} else {
		currentconfig.c_iflag &= ~IMAXBEL;
	}

	/* Apply changes/settings to the termios associated with this port. */
#if defined (__linux__)
	errno = 0;
	ret = ioctl(fd, TCSETS2, &currentconfig);
	if(ret < 0) {
		fprintf(stderr, "%s %d\n", "NATIVE configureComPortData() failed to set desired terminal settings with error number : -", errno);
		return (negative * errno);
	}

#elif defined (__APPLE__) || defined (__SunOS)
	errno = 0;
	ret  = tcsetattr(fd, TCSANOW, &currentconfig);
	if(ret < 0) {
		fprintf(stderr, "%s %d\n", "NATIVE configureComPortData() failed to set desired terminal settings with error number : -", errno);
		return (negative * errno);
	}
#endif

	return 0;
}

/*
 * Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
 * Method:    setRTS
 * Signature: (JZ)I
 */
JNIEXPORT jint JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_setRTS(JNIEnv *env, jobject obj, jlong fd, jboolean enabled) {
	jint ret = -1;
	jint negative = -1;
	jint status = -1;

	/* Get current configuration. */
	errno = 0;
	ret = ioctl(fd, TIOCMGET, &status);
	if(ret < 0) {
		fprintf(stderr, "%s %d\n", "NATIVE setRTS() failed to get current line status with error number : -", errno);
		return (negative * errno);
	}

	if(enabled == JNI_TRUE) {
		status &= ~TIOCM_RTS;
	} else {
		status |= TIOCM_RTS;
	}

	/* Update RTS line. */
	errno = 0;
	ret = ioctl(fd, TIOCMSET, &status);
	if(ret < 0) {
		fprintf(stderr, "%s %d\n", "NATIVE setRTS() failed to set requested line status with error number : -", errno);
		return (negative * errno);
	}

	return 0;
}

/*
 * Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
 * Method:    setDTR
 * Signature: (JZ)I
 */
JNIEXPORT jint JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_setDTR(JNIEnv *env, jobject obj, jlong fd, jboolean enabled) {
	jint ret = -1;
	jint negative = -1;
	jint status = -1;

	errno = 0;
	ret = ioctl(fd, TIOCMGET, &status);
	if(ret < 0) {
		fprintf(stderr, "%s %d\n", "NATIVE setDTR() failed to get current line status with error number : -", errno);
		return (negative * errno);
	}

	if(enabled == JNI_TRUE) {
		status &= ~TIOCM_DTR;
	} else {
		status |= TIOCM_DTR;
	}

	errno = 0;
	ret = ioctl(fd, TIOCMSET, &status);
	if(ret < 0) {
		fprintf(stderr, "%s %d\n", "NATIVE setDTR() failed to set requested line status with error number : -", errno);
		return (negative * errno);
	}

	return 0;
}

/*
 * Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
 * Method:    getCurrentConfigurationU
 * Signature: (J)[I
 *
 * We return the bit mask as it is with out interpretation so that application can manipulate easily using mathematics.
 */
JNIEXPORT jintArray JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_getCurrentConfigurationU(JNIEnv *env, jobject obj, jlong fd) {
	jint ret = -1;

#if defined (__linux__)
	jint settings[25];
	jintArray configuration = (*env)->NewIntArray(env, 25);

	struct termios2 currentconfig = {0};
	errno = 0;
	ret = ioctl(fd, TCGETS2, &currentconfig);
	if(ret < 0) {
		fprintf(stderr, "%s %d\n", "NATIVE getCurrentConfiguration() failed to get current configuration with error number : -", errno);
		fprintf(stderr, "%s\n", "Please try again");
		jint err[] = {-1};
		jintArray errr = (*env)->NewIntArray(env, 1);
		(*env)->SetIntArrayRegion(env, errr, 0, 1, err);
		return errr;
	}

#elif defined (__APPLE__) || defined (__SunOS)
	jint settings[23];
	jintArray configuration = env->NewIntArray(23);

	struct termios currentconfig = {0};
	errno = 0;
	ret = tcgetattr(fd, &currentconfig);
	if(ret < 0) {
		fprintf(stderr, "%s %d\n", "NATIVE getCurrentConfiguration() failed to get current configuration with error number : -", errno);
		fprintf(stderr, "%s\n", "Please try again");
		jint err[] = {-1};
		jintArray errr = (*env)->NewIntArray(env, 1);
		(*env)->SetIntArrayRegion(env, errr, 0, 1, err);
		return errr;
	}

#endif

	/* Populate array with current settings. */
#if defined (__linux__)
	settings[0] = 0;
    settings[1] = (jint) currentconfig.c_iflag;
    settings[2] = (jint) currentconfig.c_oflag;
    settings[3] = (jint) currentconfig.c_cflag;
    settings[4] = (jint) currentconfig.c_lflag;
    settings[5] = (jint) currentconfig.c_line;
    settings[6] = (jint) currentconfig.c_cc[0];
    settings[7] = (jint) currentconfig.c_cc[1];
    settings[8] = (jint) currentconfig.c_cc[2];
    settings[9] = (jint) currentconfig.c_cc[3];
    settings[10] = (jint) currentconfig.c_cc[4];
    settings[11] = (jint) currentconfig.c_cc[5];
    settings[12] = (jint) currentconfig.c_cc[6];
    settings[13] = (jint) currentconfig.c_cc[7];
    settings[14] = (jint) currentconfig.c_cc[8];
    settings[15] = (jint) currentconfig.c_cc[9];
    settings[16] = (jint) currentconfig.c_cc[10];
    settings[17] = (jint) currentconfig.c_cc[11];
    settings[18] = (jint) currentconfig.c_cc[12];
    settings[19] = (jint) currentconfig.c_cc[13];
    settings[20] = (jint) currentconfig.c_cc[14];
    settings[21] = (jint) currentconfig.c_cc[15];
    settings[22] = (jint) currentconfig.c_cc[16];
    settings[23] = (jint) currentconfig.c_ispeed;
    settings[24] = (jint) currentconfig.c_ospeed;

    (*env)->SetIntArrayRegion(env, configuration, 0, 25, settings);

#elif defined (__APPLE__) || defined (__SunOS)
	settings[0] = 0;
    settings[1] = (jint) currentconfig.c_iflag;
    settings[2] = (jint) currentconfig.c_oflag;
    settings[3] = (jint) currentconfig.c_cflag;
    settings[4] = (jint) currentconfig.c_lflag;
    settings[5] = (jint) currentconfig.c_line;
    settings[6] = (jint) currentconfig.c_cc[0];
    settings[7] = (jint) currentconfig.c_cc[1];
    settings[8] = (jint) currentconfig.c_cc[2];
    settings[9] = (jint) currentconfig.c_cc[3];
    settings[10] = (jint) currentconfig.c_cc[4];
    settings[11] = (jint) currentconfig.c_cc[5];
    settings[12] = (jint) currentconfig.c_cc[6];
    settings[13] = (jint) currentconfig.c_cc[7];
    settings[14] = (jint) currentconfig.c_cc[8];
    settings[15] = (jint) currentconfig.c_cc[9];
    settings[16] = (jint) currentconfig.c_cc[10];
    settings[17] = (jint) currentconfig.c_cc[11];
    settings[18] = (jint) currentconfig.c_cc[12];
    settings[19] = (jint) currentconfig.c_cc[13];
    settings[20] = (jint) currentconfig.c_cc[14];
    settings[21] = (jint) currentconfig.c_cc[15];
    settings[22] = (jint) currentconfig.c_cc[16];

    (*env)->SetIntArrayRegion(env, configuration, 0, 23, settings);

#endif

    return configuration;
}

/*
 * Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
 * Method:    getByteCount
 * Signature: (J)[I
 *
 * Return array's sequence is error number, number of input bytes, number of output bytes in tty buffers.
 */
JNIEXPORT jintArray JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_getByteCount(JNIEnv *env, jobject obj, jlong fd) {
	jint ret = -1;
	jint negative = -1;
	jint val[3] = {0, 0, 0};
	jintArray values = (*env)->NewIntArray(env, 3);

	errno = 0;
	ret = ioctl(fd, FIONREAD, &val[1]);
	if(ret < 0) {
		fprintf(stderr, "%s %d\n", "NATIVE getByteCount() failed to get number of bytes to read with error number : -", errno);
		val[0] = (negative * errno);
		(*env)->SetIntArrayRegion(env, values, 0, 3, val);
		return values;
	}

	errno = 0;
	ret = ioctl(fd, TIOCOUTQ, &val[2]);
	if(ret < 0) {
		fprintf(stderr, "%s %d\n", "NATIVE getByteCount() failed to get number of bytes to be written with error number : -", errno);
		val[0] = (negative * errno);
		(*env)->SetIntArrayRegion(env, values, 0, 3, val);
		return values;
	}

	return values;
}

/*
 * Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
 * Method:    clearPortIOBuffers
 * Signature: (JZZ)I
 */
JNIEXPORT jint JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_clearPortIOBuffers(JNIEnv *env, jobject obj, jlong fd, jboolean rxPortbuf, jboolean txPortbuf) {
	jint ret = -1;
	jint negative = -1;
	jint PORTIOBUFFER = -1;

	if( (rxPortbuf == JNI_TRUE) && (txPortbuf == JNI_TRUE) ) {
			PORTIOBUFFER = TCIOFLUSH;
	} else if (rxPortbuf == JNI_TRUE) {
			PORTIOBUFFER = TCIFLUSH;
	} else if (txPortbuf == JNI_TRUE) {
			PORTIOBUFFER = TCOFLUSH;
	}

	errno = 0;
	ret = ioctl(fd , TCFLSH, PORTIOBUFFER);
	if(ret < 0) {
		fprintf(stderr, "%s %d\n", "NATIVE clearPortIOBuffers() failed to clear requested buffer(s) with error number : -", errno);
		fprintf(stderr, "%s\n", "Please try again !");
		return (negative * errno);
	}

	return 0;
}

/*
 * Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
 * Method:    getLinesStatus
 * Signature: (J)[I
 *
 * The status of modem/control lines is returned as array of integers where '1' means line is asserted and '0' means de-asserted.
 * The sequence of lines matches in both java layer and native layer.
 */
JNIEXPORT jintArray JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_getLinesStatus(JNIEnv *env, jobject obj, jlong fd) {

	jint ret = -1;
	jint negative = -1;
	jint status[8] = {0};
	jint lines_status = 0;
	jintArray current_status = (*env)->NewIntArray(env, 8);

	errno = 0;
    ret = ioctl(fd, TIOCMGET, &lines_status);
	if(ret < 0) {
		fprintf(stderr, "%s %d\n", "NATIVE getLinesStatus() failed to get status of control lines with error number : -", errno);
		status[0] = (negative * errno);
		(*env)->SetIntArrayRegion(env, current_status, 0, 8, status);
		return current_status;
	}

	status[0] = 0;
	status[1] = (lines_status & TIOCM_CTS)  ? 1 : 0;
	status[2] = (lines_status & TIOCM_DSR)  ? 1 : 0;
	status[3] = (lines_status & TIOCM_CD)   ? 1 : 0;
	status[4] = (lines_status & TIOCM_RI)   ? 1 : 0;
	status[5] = (lines_status & TIOCM_LOOP) ? 1 : 0;
	status[6] = (lines_status & TIOCM_RTS)  ? 1 : 0;
	status[7] = (lines_status & TIOCM_DTR)  ? 1 : 0;
	(*env)->SetIntArrayRegion(env, current_status, 0, 8, status);

	return current_status;
}

/*
 * Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
 * Method:    sendBreak
 * Signature: (JI)I
 *
 * The duration is in milliseconds.
 */
JNIEXPORT jint JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_sendBreak(JNIEnv *env, jobject obj, jlong fd, jint duration) {
	jint ret = -1;
	jint negative = -1;

	/* Start break condition. */
	errno = 0;
	ret = ioctl(fd, TIOCSBRK, 0);
	if(ret < 0) {
		fprintf(stderr, "%s %d\n", "NATIVE sendBreak() failed to start break condition with error number : -", errno);
		return (negative * errno);
	}

	/* Duration must be in micro-seconds. */
	serial_delay(duration);

	/* Stop break condition. */
	errno = 0;
	ret = ioctl(fd, TIOCCBRK, 0);
	if(ret < 0) {
		fprintf(stderr, "%s %d\n", "NATIVE sendBreak() failed to stop break condition with error number : -", errno);
		return (negative * errno);
	}

	return 0;
}

/*
 * Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
 * Method:    getInterruptCount
 * Signature: (J)[I
 *
 * This is called when the user wants to know how many serial line interrupts have happened. If the driver has an interrupt
 * handler, it should define an internal structure of counters to keep track of these statistics and increment the proper
 * counter every time the function is run by the kernel. This ioctl call passes the kernel a pointer to a structure
 * serial_icounter_struct , which should be filled by the tty driver.
 *
 * Not supported on Solaris and Mac OS. This will return 0 for all the indexes.
 */
JNIEXPORT jintArray JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_getInterruptCount(JNIEnv *env, jobject obj, jlong fd) {
#if defined (__linux__)
	jint ret = -1;
	jint negative = -1;
	struct serial_icounter_struct counter = {0};
	jint count_info[12] = {0};
	jintArray interrupt_info = (*env)->NewIntArray(env, 12);

	errno = 0;
	ret = ioctl(fd , TIOCGICOUNT, &counter);
	if(ret < 0) {
		count_info[0] = (negative * errno);
		(*env)->SetIntArrayRegion(env, interrupt_info, 0, 12, count_info);
		return interrupt_info;
	}

	count_info[1] = counter.cts;
	count_info[2] = counter.dsr;
	count_info[3] = counter.rng;
	count_info[4] = counter.dcd;
	count_info[5] = counter.rx;
	count_info[6] = counter.tx;
	count_info[7] = counter.frame;
	count_info[8] = counter.overrun;
	count_info[9] = counter.parity;
	count_info[10] = counter.brk;
	count_info[11] = counter.buf_overrun;

	(*env)->SetIntArrayRegion(env, interrupt_info, 0, 12, count_info);
	return interrupt_info;
#elif defined (__APPLE__) || defined (__SunOS)
	jint count_info[12] = {0};
	jintArray interrupt_info = (*env)->NewIntArray(env, 12);
	(*env)->SetIntArrayRegion(env, interrupt_info, 0, 12, count_info);
	return interrupt_info;
#endif
}

/*
 * Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
 * Method:    setMinDataLength
 * Signature: (JI)I
 *
 * This function changes the behaviour of when data listener is called based on the value of numOfBytes variable.
 * The listener will be called only when this many bytes will be available to read from file descriptor.
 */
JNIEXPORT jint JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_setMinDataLength(JNIEnv *env, jobject obj, jlong fd, jint numOfBytes) {
	jint ret = 0;
	jint negative = -1;

#if defined (__linux__)
	struct termios2 currentconfig = {0};
	errno = 0;
	ret = ioctl(fd, TCGETS2, &currentconfig);
	if(ret < 0) {
		fprintf(stderr, "%s %d\n", "NATIVE setMinDataLength() failed to get current configuration with error number : -", errno);
		fprintf(stderr, "%s\n", "Please try again !");
		return (negative * errno);
	}
#elif defined (__APPLE__) || defined (__SunOS)
	struct termios currentconfig = {0};
	errno = 0;
	ret = tcgetattr(fd, &currentconfig);
	if(ret < 0) {
		fprintf(stderr, "%s %d\n", "NATIVE setMinDataLength() failed to get current configuration with error number : -", errno);
		fprintf(stderr, "%s\n", "Please try again !");
		return (negative * errno);
	}
#endif

	currentconfig.c_cc[VMIN] = numOfBytes;

#if defined (__linux__)
	errno = 0;
	ret = ioctl(fd, TCSETS2, &currentconfig);
	if(ret < 0) {
		fprintf(stderr, "%s %d\n", "NATIVE setMinDataLength() failed to set default terminal settings with error number : -", errno);
		return (negative * errno);
	}
#elif defined (__APPLE__) || defined (__SunOS)
	errno = 0;
	ret  = tcsetattr(fd, TCSANOW, &currentconfig);
	if(ret < 0) {
		fprintf(stderr, "%s %d\n", "NATIVE setMinDataLength() failed to set default terminal settings with error number : -", errno);
		return (negative * errno);
	}
#endif

	return 0;
}

/*
 * Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
 * Method:    setUpDataLooperThread
 * Signature: (JLcom/embeddedunveiled/serial/SerialComLooper;)I
 *
 * Note that, GetMethodID() causes an uninitialised class to be initialised. However in our case we have already initialised classes required.
 */
JNIEXPORT jint JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_setUpDataLooperThread(JNIEnv *env, jobject obj, jlong fd, jobject looper) {
	jint ret = -1;
	jint negative = -1;

	jint x = -1;
	struct com_thread_params *ptr;
	ptr = fd_looper_info;
	jboolean entry_found = JNI_FALSE;

	pthread_t thread_id;
	struct com_thread_params params;
	pthread_attr_t attr;
	void *arg;

	/* we make sure that thread creation, data passing and access to global data is atomic. */
	pthread_mutex_lock(&mutex);

	/* Check if there is an entry for this fd already in global array. If yes, we will update that with information about data thread. */
	for(x=0; x<MAX_NUM_THREADS; x++) {
		if(ptr->fd == fd) {
			entry_found = JNI_TRUE;
			break;
		}
		ptr++;
	}

	if(entry_found == JNI_TRUE) {
		/* Set up pointer to location which will be passed to thread. */
		arg = &fd_looper_info[x];
	} else {
		/* Set the values, create reference to it to be passed to thread. */
		jobject datalooper = (*env)->NewGlobalRef(env, looper);
		if(datalooper == NULL) {
			fprintf(stderr, "%s \n", "NATIVE setUpDataLooperThread() could not create global reference for looper object.");
			return -240;
		}
		params.jvm = jvm;
		params.fd = fd;
		params.looper = datalooper;
		params.data_thread_id = 0;
		params.event_thread_id = 0;
		params.mutex = mutex;

		/* Create and save eventfd which will be used when listener is unregistered. */
#if defined (__linux__)
		errno = 0;
		ret  = eventfd(0, O_NONBLOCK);
		if(ret < 0) {
			fprintf(stderr, "%s %d\n", "NATIVE setUpDataLooperThread() failed to create eventfd with error number : -", errno);
			return (negative * errno);
		}
		params.evfd = ret;

#elif defined (__APPLE__) || defined (__SunOS)
		/*TODO evfd for apple and sun*/
#endif

		fd_looper_info[dtp_index] = params;
		arg = &fd_looper_info[dtp_index];
	}

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	errno = 0;
	ret = pthread_create(&thread_id, NULL, &data_looper, arg);
	if(ret < 0) {
		fprintf(stderr, "%s %d\n", "NATIVE setUpDataLooperThread() failed to create native data looper thread with error number : -", errno);
		return (negative * errno);
	}

	/* Save the data thread id which will be used when listener is unregistered. */
	((struct com_thread_params*) arg)->data_thread_id = thread_id;

	if(entry_found == JNI_TRUE) {
		/* index has been already incremented when data looper thread was created, so do nothing. */
	} else {
		/* update address where parameters for next thread will be stored. */
		dtp_index++;
	}

	pthread_mutex_unlock(&mutex);

	return 0;
}

/*
 * Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
 * Method:    destroyDataLooperThread
 * Signature: (J)I
 */
JNIEXPORT jint JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_destroyDataLooperThread(JNIEnv *env, jobject obj, jlong fd) {
	int ret = -1;
	int negative = -1;
	int x = -1;
	struct com_thread_params *ptr;
	ptr = fd_looper_info;
	unsigned long int data_thread_id = 0;
	void *status;
#if defined (__linux__)
	uint64_t value = 1;
#elif defined (__APPLE__) || defined (__SunOS)
	/*TODO evfd for apple and sun*/
#endif

	pthread_mutex_lock(&mutex);

	/* Find the data thread serving this file descriptor. */
	for(x=0; x<MAX_NUM_THREADS; x++) {
		if(ptr->fd == fd) {
			data_thread_id = ptr->data_thread_id;
			break;
		}
		ptr++;
	}

	/* First tell thread to get cancelled. */
	ret = pthread_cancel(data_thread_id);
	if(ret != 0) {
		fprintf(stderr, "%s %d\n", "NATIVE destroyDataLooperThread() failed to destroy native data looper thread with error number : -", errno);
		return -240;
	}

#if defined (__linux__)
	/* If the data looper thread is waiting for an event, let us cause an event to happen, so thread come out of waiting on fd and
	* can check that it has been cancelled. */
	ret = write(ptr->evfd, &value, sizeof(value));
#elif defined (__APPLE__) || defined (__SunOS)
	/*TODO evfd for apple and sun*/
#endif

	/* Join the thread to check its exit status. */
	ret = pthread_join(data_thread_id, &status);
	if(status == PTHREAD_CANCELED) {
		fprintf(stderr, "%s \n", "cancelled");
	}

	if( (ret != 0) || (status != PTHREAD_CANCELED) ) {
		fprintf(stderr, "%s %d\n", "NATIVE destroyDataLooperThread() failed to join/cancel native data looper thread with error number : -", ret);
		return (negative * ret);
	}

	pthread_mutex_unlock(&mutex);
	return 0;
}

/*
 * Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
 * Method:    setUpEventLooperThread
 * Signature: (JLcom/embeddedunveiled/serial/SerialComLooper;)I
 */
JNIEXPORT jint JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_setUpEventLooperThread(JNIEnv *env, jobject obj, jlong fd, jobject looper) {
	jint ret = -1;
	jint negative = -1;

	jint x = -1;
	struct com_thread_params *ptr;
	ptr = fd_looper_info;
	jboolean entry_found = JNI_FALSE;

	pthread_t thread_id;
	struct com_thread_params params;
	pthread_attr_t attr;
	void *arg;

	/* we make sure that thread creation, data passing and access to global data is atomic. */
	pthread_mutex_lock(&mutex);

	/* Check if there is an entry for this fd already in global array. If yes, we will update that with information about event thread. */
	for(x=0; x<MAX_NUM_THREADS; x++) {
		if(ptr->fd == fd) {
			entry_found = JNI_TRUE;
			break;
		}
		ptr++;
	}

	if(entry_found == JNI_TRUE) {
		/* Set up pointer to location which will be passed to thread. */
		arg = &fd_looper_info[x];
	} else {
		/* Set the values, create reference to it to be passed to thread. */
		jobject eventlooper = (*env)->NewGlobalRef(env, looper);
		if(eventlooper == NULL) {
			fprintf(stderr, "%s \n", "NATIVE setUpDataLooperThread() could not create global reference for looper object.");
			return -240;
		}
		params.jvm = jvm;
		params.fd = fd;
		params.looper = eventlooper;
		params.data_thread_id = 0;
		params.event_thread_id = 0;
		params.mutex = mutex;
		fd_looper_info[dtp_index] = params;
		arg = &fd_looper_info[dtp_index];
	}

	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	errno = 0;
	ret = pthread_create(&thread_id, NULL, &event_looper, arg);
	if(ret < 0) {
		fprintf(stderr, "%s %d\n", "NATIVE setUpDataLooperThread() failed to create native data looper thread with error number : -", errno);
		return (negative * errno);
	}

	/* Save the data thread id which will be used when listener is unregistered. */
	((struct com_thread_params*) arg)->event_thread_id = thread_id;

	if(entry_found == JNI_TRUE) {
		/* index has been already incremented when data looper thread was created, so do nothing. */
	} else {
		/* update address where parameters for next thread will be stored. */
		dtp_index++;
	}

	pthread_mutex_unlock(&mutex);
	return 0;
}


/*
 * Class:     com_embeddedunveiled_serial_SerialComJNINativeInterface
 * Method:    destroyEventLooperThread
 * Signature: (J)I
 */
JNIEXPORT jint JNICALL Java_com_embeddedunveiled_serial_SerialComJNINativeInterface_destroyEventLooperThread(JNIEnv *env, jobject obj, jlong fd) {
	int ret = -1;
	int negative = -1;
	int x = -1;
	struct com_thread_params *ptr;
	ptr = fd_looper_info;
	unsigned long int event_thread_id = 0;
	void *status;

	/* Find the event thread serving this file descriptor. */
	for(x=0; x<MAX_NUM_THREADS; x++) {
		if(ptr->fd == fd) {
			event_thread_id = ptr->event_thread_id;
			break;
		}
		ptr++;
	}

	/* First tell thread to get cancelled. */
	errno = 0;
	ret = pthread_cancel(event_thread_id);
	if(ret != 0) {
		fprintf(stderr, "%s %d\n", "NATIVE destroyEventLooperThread() failed to destroy native event looper thread with error number : -", errno);
		return -240;
	}

	/* Join the thread to check its exit status. */
	ret = pthread_join(event_thread_id, &status);
	if(status == PTHREAD_CANCELED) {
		fprintf(stderr, "%s \n", "cancelled");
	}

	if( (ret != 0) || (status != PTHREAD_CANCELED) ) {
		fprintf(stderr, "%s %d\n", "NATIVE destroyEventLooperThread() failed to join/cancel native event looper thread with error number : -", ret);
		return (negative * ret);
	}

	return 0;
}

#endif /* End compiling for Unix-like OS. */
