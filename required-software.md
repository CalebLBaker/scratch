
| Command Name  | Possible Containing Package | Needed to build | Needed to run in vm | Needed to clean up build files | Needed to debug |
|---------------|-----------------------------|-----------------|---------------------|--------------------------------|-----------------|
| rm            | coreutils                   | No              | Yes                 | Yes                            | No              |
| echo          | coreutils                   | No              | Yes                 | Yes                            | No              |
| touch         | coreutils                   | No              | Yes                 | Yes                            | No              |
| chmod         | coreutils                   | No              | Yes                 | Yes                            | No              |
| bash          | bash                        | No              | Yes                 | Yes                            | No              |
| gcc           | gcc                         | Yes             | Yes                 | No                             | Yes             |
| make          | make                        | Yes             | Yes                 | Yes                            | Yes             |
| grub-mkrescue | grub                        | No              | Yes                 | Yes                            | No              |
| xorriso       | libisoburn                  | No              | Yes                 | Yes                            | No              |
| mformat       | mtools                      | No              | Yes                 | Yes                            | No              |
| VBoxManage    | virtualbox                  | No              | Yes                 | Yes                            | No              |
| gdb           | gdb                         | No              | No                  | No                             | Yes             |

