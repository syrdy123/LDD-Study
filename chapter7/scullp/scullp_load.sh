#!/bin/sh
module="scullp"
device="scullp"
param1="scullp_major"
module_path="/home/ubuntu/workspace/driver_practice/chapter7/scullp/scullp.ko"

# Remove existing device nodes
rm -f /dev/${device}[0-3]

# Check if the module is already loaded
if lsmod | grep -q "^$module"; then
    echo "Removing old module $module"
    rmmod $module
fi

# Load the new module
if [ -n "$1" ]; then
    echo "Loading $module with parameter $param1=$1"
    insmod $module_path $param1=$1
else
    echo "Loading $module without parameters"
    insmod $module_path
fi

# Wait a moment for the device to be registered
sleep 1

# Get the major number for the module
major=$(cat /proc/devices | grep "^[0-9]* $module$" | awk '{print $1}')

if [ -z "$major" ]; then
    echo "Failed to get major number for $module"
    exit 1
fi

echo "Major number: $major"

# Create device nodes with proper permissions
for i in 0 1 2 3; do
    echo "Creating device node: /dev/${device}$i"
    mknod /dev/${device}$i c $major $i
    chmod 666 /dev/${device}$i
done

echo "Device nodes created successfully"
