#include "../kernel/types.h"
#include "../kernel/stat.h"
#include "user.h"
#include "../kernel/fcntl.h"
#include "../kernel/memlayout.h"
// #include "../kernel/mmu.h"
#include "../kernel/param.h"
#include "../kernel/spinlock.h"
#include "../kernel/sleeplock.h"
#include "../kernel/fs.h"
// #include "../kernel/proc.h"
#include "../kernel/syscall.h"

int main(int argc, char **argv) {
    /*
    int fd = open("README", O_RDWR);
    printf("fd: %d\n", fd);

    char* text = (char*)mmap(0, 8192, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_POPULATE, fd, 0);

    printf("freemem is %d\n",freemem());
    printf("return addr is %ld\n", (uint64)text);

    // printf("text[0] is: ");
    // char ch = text[0];
    // write(1, &ch, 1); // 1바이트만 출력
    printf("\n");

    munmap((uint64)text);
    printf("freemem is %d\n",freemem());
    close(fd);

    exit(0);
}
    */
    int flag = 0;
    char* test, *test2, *test3, *test4;
    int option = atoi(argv[1]);
    int fd = open("README",O_RDWR);
    char ch;
    printf("fd is %d option %d\n",fd, option);
    switch(option){
        case 1:
        goto ANONY;
        break;
        case 2:
        goto FILEMAP;
        break;
        default:
        goto FORK;
    ANONY:
        printf("FIRST freemem now is %d\n",freemem());
        test = (char*)mmap(0, 4096, PROT_READ|PROT_WRITE,MAP_POPULATE|MAP_ANONYMOUS, -1, 0);
        printf( "- addr: %lx\n", (uint64)test);
        printf("SECOND freemem now is %d\n",freemem());
        test2 = (char*)mmap(4096,4096,PROT_READ|PROT_WRITE,MAP_ANONYMOUS,-1,0);
        printf( "- addr: %lx\n", (uint64)test2);

        printf( "LAST freemem now is %d\n",freemem());
        printf( "ANONYMOUS test done\n");

        int x;
        x = munmap((uint64)test);
        printf("0: %d unmap results\n",x);
        printf("freemem now is %d\n",freemem());
        x = munmap((uint64)test2);
        printf("0: %d unmap results\n",x);
        printf("freemem now is %d\n",freemem());

        close(fd);
        exit(0);
        return 1;
    FILEMAP:
        printf("THIRD freemem now is %d\n",freemem());
        test3 = (char *)mmap(8192, 4096, PROT_READ|PROT_WRITE,MAP_POPULATE, fd, 0);
        test3[2286]='\0';
        printf( "- addr: %lx\n", (uint64)test3);
        ch = test3[0];
        printf( "- fd data: ");
        write(1, &ch, 1);
        ch = test3[1];
        write(1, &ch, 1);
        ch = test3[2];
        write(1, &ch, 1);
        printf("\n");
        printf("FOURTH freemem now is %d\n",freemem());
        test4 = (char*)mmap(16384,4096,PROT_READ|PROT_WRITE,0,fd,0);
        test4[2286]='\0';
        printf( "- addr: %lx\n", (uint64)test4);
        printf( "- pf data: ");
        ch = test4[0];
        write(1, &ch, 1);
        ch = test4[1];
        write(1, &ch, 1);
        ch = test4[2];
        write(1, &ch, 1);
        printf("\n");
        printf("LAST freemem now is %d\n",freemem());
        printf( "FILEMAP test done\n");
        
        x = munmap((uint64)test3);
        printf("0: %d unmap results\n",x);
        printf("freemem now is %d\n",freemem());
        x = munmap((uint64)test4);
        printf("0: %d unmap results\n",x);
        printf("freemem now is %d\n",freemem());
        close(fd);
        exit(0);
        return 0;
    FORK:
        printf("FIRST freemem now is %d\n",freemem());
        test = (char*)mmap(0, 4096, PROT_READ|PROT_WRITE,MAP_POPULATE|MAP_ANONYMOUS, -1, 0);
        printf( "- addr: %lx\n", (uint64)test);
    
        printf("SECOND freemem now is %d\n",freemem());
        test2 = (char*)mmap(4096,4096,PROT_READ|PROT_WRITE,MAP_ANONYMOUS,-1,0);
        printf( "- addr: %lx\n", (uint64)test2);
    
        printf("THIRD freemem now is %d\n",freemem());
        test3 = (char *)mmap(8192, 4096, PROT_READ|PROT_WRITE,MAP_POPULATE, fd, 0);
        test3[2286]='\0';
        printf( "- addr: %lx\n", (uint64)test3);
        ch = test3[0];
        printf( "- fd data: ");
        write(1, &ch, 1);
        ch = test3[1];
        write(1, &ch, 1);
        ch = test3[2];
        write(1, &ch, 1);
        printf("\n");
        printf("FOURTH freemem now is %d\n",freemem());
        test4 = (char*)mmap(16384,4096,PROT_READ|PROT_WRITE,0,fd,0);
        test4[2286]='\0';
        printf( "- addr: %lx\n", (uint64)test4);
        printf( "- pf data: ");
        ch = test4[0];
        write(1, &ch, 1);
        ch = test4[1];
        write(1, &ch, 1);
        ch = test4[2];
        write(1, &ch, 1);
        printf("\n");
        printf("LAST freemem now is %d\n",freemem());
        flag = 1;
        printf("\n\n\n!!fork start!\n\n\n");
        if(flag == 1){
            int f;
            if((f=fork())==0){
                printf( "\n\nCHILD START\n");
                int x;
                int base = 0x40000000;
                printf("Before munmap, freemem: %d\n", freemem());
                
                x = munmap(0+base);
                printf("0: %d unmap results\n",x);
                printf("freemem now is %d\n",freemem());
                x = munmap(4096+base);
                printf("4096: %d unmap results\n",x);
                printf("freemem now is %d\n",freemem());
                
                // printf( "- fd data: %c %c %c\n", test3[0], test3[1], test3[2]);
                ch = test3[0];
                printf( "- fd data: ");
                write(1, &ch, 1);
                ch = test3[1];
                write(1, &ch, 1);
                ch = test3[2];
                write(1, &ch, 1);
                printf("\n");
                x = munmap(8192+base);
                printf("8192: %d unmap results\n",x);
                printf("freemem now is %d\n",freemem());
                
                // printf( "- pf data: %c %c %c\n", test4[0], test4[1], test4[2]);
                ch = test4[0];
                printf( "- pf data: ");
                write(1, &ch, 1);
                ch = test4[1];
                write(1, &ch, 1);
                ch = test4[2];
                write(1, &ch, 1);
                printf("\n");
                x = munmap(16384+base);
                printf("16384: %d unmap results\n",x);
                printf("freemem now is %d\n",freemem());
                
                
                exit(0);
                return 0;               

            }
            else{
                wait(0);
                printf( "\n\nPARENT START\n");
                int x;
                int base = 0x40000000;
                x = munmap(0+base);
                printf("0: %d unmap results\n",x);
                printf("freemem now is %d\n",freemem());
                x = munmap(4096+base);
                printf("4096: %d unmap results\n",x);
                printf("freemem now is %d\n",freemem());
                x = munmap(8192+base);
                printf("8192: %d unmap results\n",x);
                printf("freemem now is %d\n",freemem());
                x = munmap(16384+base);
                printf("16384: %d unmap results\n",x);
                printf("freemem now is %d\n",freemem());
            }
        }
    printf("Lastly freemem now is %d\n",freemem());
    close(fd);
    exit(0);
    return 0;
    }
}
 
