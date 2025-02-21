#!/bin/sh
module="scull_ioctl"
device="scull_ioctl"
param1="scull_major"
module_path="/home/ubuntu/workspace/driver_practice/scull_ioctl/scull_ioctl.ko"


# Remove existing device nodes
rm -f /dev/${device}[0-3]

# Check if the module is already loaded
oldmodule=$(awk "\$2==\"$module\" {print \$2}" /proc/devices)
   
if [ -z "$oldmodule" ]; then
    echo "No such module is loaded."
else
    echo "oldmodule = $oldmodule"
fi

# Unload the old module if necessary
if [ "$oldmodule" = "$module" ]; then
    echo "Removing old module $oldmodule"
    rmmod $module_path
fi

# Load the new module
if [ -n "$1" ]; then
    echo "Loading $module with parameter $param1=$1"
    insmod $module_path $param1=$1
else
    echo "Loading $module without parameters"
    insmod $module_path
fi

# Get the major number for the module
major=$(awk "\$2==\"$module\" {print \$1}" /proc/devices)
echo "major = $major"

# Create device nodes
mknod /dev/${device}0 c $major 0 
mknod /dev/${device}1 c $major 1
mknod /dev/${device}2 c $major 2
mknod /dev/${device}3 c $major 3
