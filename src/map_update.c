/*    Copyright (C) 2022  Robert Caamano   */
 /*
  *
  *   This program inserts a rule into an existing pinned 
  *   zt_tproxy_map hash table created by the redirect_udp
  *   program when attatched to an interface via tc
  *
  *   This program is free software: you can redistribute it and/or modify
  *   it under the terms of the GNU General Public License as published by
  *   the Free Software Foundation, either version 3 of the License, or
  *   (at your option) any later version.

  *   This program is distributed in the hope that it will be useful,
  *   but WITHOUT ANY WARRANTY; without even the implied warranty of
  *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  *   GNU General Public License for more details.
  *   see <https://www.gnu.org/licenses/>.
*/

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <stdbool.h>
#include <ctype.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <linux/bpf.h>

#define MAX_INDEX_ENTRIES  100
#define MAX_TABLE_SIZE  65536

struct tproxy_tcp_port_mapping {
    __u16 low_port;
    __u16 high_port;
    __u16 tproxy_port;
    __u32 tproxy_ip;
};

struct tproxy_udp_port_mapping {
    __u16 low_port;
    __u16 high_port;
    __u16 tproxy_port;
    __u32 tproxy_ip;
};

struct tproxy_tuple {
    __u32 dst_ip;
	__u32 src_ip;
    __u16 udp_index_len;
    __u16 tcp_index_len;
    __u16 udp_index_table[MAX_INDEX_ENTRIES];
    __u16 tcp_index_table[MAX_INDEX_ENTRIES];
    struct tproxy_udp_port_mapping udp_mapping[MAX_TABLE_SIZE];
    struct tproxy_tcp_port_mapping tcp_mapping[MAX_TABLE_SIZE];
};

struct tproxy_key {
           __u32  dst_ip;
		   __u16  prefix_len;
           __u16  pad;
};

int32_t ip2l(char *ip){
    char *endPtr;
    int32_t byte1 = strtol(ip,&endPtr,10);
    if((byte1 <= 0) || (byte1 > 223) || (!isdigit(*(endPtr + 1)))){
        printf("Invalid IP Address: %s\n",ip);
        exit(1);	
    }
    int32_t byte2 = strtol(endPtr + 1,&endPtr,10);
    if((byte2 < 0) || (byte2 > 255) || (!isdigit(*(endPtr + 1)))){
       printf("Invalid IP Address: %s\n",ip);
       exit(1);
    }
    int32_t byte3 = strtol(endPtr + 1,&endPtr,10);
    if((byte3 < 0) || (byte3 > 255) || (!isdigit(*(endPtr + 1)))){
       printf("Invalid IP Address: %s\n",ip);
       exit(1);
    }
    int32_t byte4 = strtol(endPtr + 1,&endPtr,10);
    if((byte4 < 0) || (byte4 > 255) || (!(*(endPtr) == '\0'))){
       printf("Invalid IP Address: %s\n",ip);
       exit(1);
    }
    return (byte1 << 24) + (byte2 << 16) + (byte3 << 8) + byte4;
}

unsigned short port2s(char *port){
    char *endPtr;
    int32_t tmpint = strtol(port,&endPtr,10);
    if((tmpint <=0) || (tmpint > 65535) || (!(*(endPtr) == '\0'))){
       printf("Invalid Port: %s\n", port);
       exit(1);
    }
    unsigned short usint = (unsigned short)tmpint;
    return usint;
}

__u8 proto2u8(char *protocol){
    char *endPtr;
    int32_t tmpint = strtol(protocol,&endPtr,10);
    if((tmpint <=0) || (tmpint > 255) || (!(*(endPtr) == '\0'))){
       printf("Invalid Protocol: %s\n", protocol);
       exit(1);
    }
    __u8 usint = (__u8)tmpint;
    return usint;
}

__u16 len2u16(char *len){
    char *endPtr;
    int32_t tmpint = strtol(len,&endPtr,10);
    if((tmpint <= 0) || (tmpint > 32) || (!(*(endPtr) == '\0'))){
       printf("Invalid Prefix Length: %s\n", len);
       exit(1);
    }
    __u16 u16int = (__u16)tmpint;
    return u16int;
}



void remove_udp_index(__u16 index, struct tproxy_udp_port_mapping *mapping, struct tproxy_tuple *tuple){
    bool is_new = true;
    for (int x = 0; x < tuple->udp_index_len ; x++){
        if(tuple->udp_index_table[x] == index){
            is_new = false;
        }
    }
    if(is_new){
        tuple->tcp_index_table[tuple->udp_index_len] = index;
        tuple->udp_index_len +=1;
    }
    memcpy((void *)&tuple->udp_mapping[index],(void *)mapping,sizeof(struct tproxy_udp_port_mapping));
}

void add_udp_index(__u16 index, struct tproxy_udp_port_mapping *mapping, struct tproxy_tuple *tuple){
    bool is_new = true;
    for (int x = 0; x < tuple->udp_index_len ; x++){
        if(tuple->udp_index_table[x] == index){
            is_new = false;
        }
    }
    if(is_new){
        //printf("index=%d\n",ntohs(index));
        tuple->udp_index_table[tuple->udp_index_len] = index;
        //printf("map_udp_index =%d\n", ntohs(tuple->udp_index_table[tuple->udp_index_len]));
        tuple->udp_index_len +=1;
    }
    memcpy((void *)&tuple->udp_mapping[index],(void *)mapping,sizeof(struct tproxy_udp_port_mapping));
}

void add_tcp_index(__u16 index, struct tproxy_tcp_port_mapping *mapping, struct tproxy_tuple *tuple){
    bool is_new = true;
    for (int x = 0; x < tuple->tcp_index_len ; x++){
        if(tuple->tcp_index_table[x] == index){
            is_new = false;
        }
    }
    if(is_new){
        tuple->tcp_index_table[tuple->tcp_index_len] = index;
        tuple->tcp_index_len +=1;
    }
    memcpy((void *)&tuple->tcp_mapping[index],(void *)mapping,sizeof(struct tproxy_tcp_port_mapping));
}

int main(int argc, char **argv){
    union bpf_attr map;
    const char *path = "/sys/fs/bpf/tc/globals/zt_tproxy_map";
    if (argc < 7) {
        fprintf(stderr, "Usage: %s <ip dest address or prefix> <prefix length> <dst_port> <src_port> <tproxy_port> <protocol id>\n", argv[0]);
        exit(0);
    }
    __u8 protocol = proto2u8(argv[6]);
    struct tproxy_key key = {htonl(ip2l(argv[1])), len2u16(argv[2]),0};
    struct tproxy_tuple orule;
    //Open BPF zt_tproxy_map map
    memset(&map, 0, sizeof(map));
    map.pathname = (uint64_t) path;
    map.bpf_fd = 0;
    map.file_flags = 0;
    int fd = syscall(__NR_bpf, BPF_OBJ_GET, &map, sizeof(map));
    if (fd == -1){
	printf("BPF_OBJ_GET: %s \n", strerror(errno));
        exit(1);
    }
    map.map_fd = fd;
    map.key = (uint64_t)&key;
    map.value = (uint64_t)&orule;
    int lookup = syscall(__NR_bpf, BPF_MAP_LOOKUP_ELEM, &map, sizeof(map));
    unsigned short index = htons(port2s(argv[3]));
    struct tproxy_udp_port_mapping udp_mapping = {
        htons(port2s(argv[3])),
        htons(port2s(argv[4])),
        htons(port2s(argv[5])),
        0x0100007f
        };
    struct tproxy_tcp_port_mapping tcp_mapping = {
        htons(port2s(argv[3])),
        htons(port2s(argv[4])),
        htons(port2s(argv[5])),
        0x0100007f
        };
    if(lookup){
        if(protocol == 17){
            struct tproxy_tuple rule = {
                htonl(ip2l(argv[1])),
                0x0,//zero source address
                1,
                0,
                {index},
                {},
                {},
                {}
            };
            memcpy((void *)&rule.udp_mapping[index],(void *)&udp_mapping,sizeof(struct tproxy_udp_port_mapping));
            map.value = (uint64_t)&rule;
            if(!rule.udp_mapping[index].low_port){
                printf("memcpy failed");
                exit(1);
            }
        }else if(protocol==6){
            struct tproxy_tuple rule = {
                htonl(ip2l(argv[1])),
                0x0,//zero source address
                0,
                1,
                {},
                {index},
                {},
                {}
            };
            memcpy((void *)&rule.tcp_mapping[index],(void *)&tcp_mapping,sizeof(struct tproxy_tcp_port_mapping));
            map.value = (uint64_t)&rule;
            if(!rule.tcp_mapping[index].low_port){
                printf("memcpy failed");
                exit(1);
            }
        }else{
            printf("Unsupported Protocol");
            exit(1);
        }
    }else{
        printf("lookup success\n");
        if(protocol == 17){
            add_udp_index(index, &udp_mapping, &orule);
            if(!(orule.udp_mapping[index].low_port == index)){
                printf("memcpy failed");
                exit(1);
            }
        }
        else if(protocol == 6){
            add_tcp_index(index, &tcp_mapping, &orule);
            if(!orule.tcp_mapping[index].low_port){
                printf("memcpy failed");
                exit(1);
            }
        }else{
            printf("Unsupported Protocol\n");
            exit(1);
        }
        /*if (orule.udp_mapping[index].low_port == index){
            printf("udp_mapping[%d].low_port = %d\n", index,ntohs(orule.udp_mapping[index].low_port));
        }
        else{
            printf("udp_mapping[5060] does not exist\n");
        }*/
    }
    map.flags = BPF_ANY;
    int result = syscall(__NR_bpf, BPF_MAP_UPDATE_ELEM, &map, sizeof(map));
    if (result){
	printf("MAP_UPDATE_ELEM: %s \n", strerror(errno));
        exit(1);
    }
    close(fd);
}
