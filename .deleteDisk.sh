#!/bin/bash
echo "VBoxManage storageattach scratch --storagectl SATA ... --meium emptydrive ..."
VBoxManage storageattach scratch --storagectl SATA --port 0 --device 0 --type hdd --medium emptydrive 2> /dev/null
echo "VBoxManage closemedium disk scratch.vdi --delete 2> /dev/null"
VBoxManage closemedium disk scratch.vdi --delete 2> /dev/null
echo "rm -f .attach scratch.vdi"
rm -f .attach scratch.vdi
exit 0
