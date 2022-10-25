  This is a project to develop an ebpf program that 
  utilizes tc-bpf to strip the UDP Outer Header on ingress ipv4 udp flows 
  if a geneve header is detected.

  prereqs: Ubuntu 22.04 server

           sudo apt update

           sudo apt upgrade

           sudo reboot

           sudo apt install -y gcc clang libc6-dev-i386 libbpfcc-dev libbpf-dev

  compile:

        clang -O2 -Wall -Wextra -target bpf -c -o redirect_udp.o redirect_udp.c
  
  attach:
        
        sudo tc qdisc add dev <interface name>  clsact

        sudo tc filter add dev <interface name> ingress bpf da obj redirect_udp.o sec sk_udp_redirect

  detach:

        sudo tc qdisc del dev <interface name>  clsact
 
  Example: Monitor ebpf trace messages

           sudo cat /sys/kernel/debug/tracing/trace_pipe
           
           <idle>-0       [001] d.s.. 69289.977151: bpf_trace_printk: prefix_len=0x18
           <idle>-0       [001] dNs.. 69289.977183: bpf_trace_printk: match on dest=ac10f000
           <idle>-0       [001] dNs.. 69289.977184: bpf_trace_printk: match on dest_port=5060
           <idle>-0       [001] dNs.. 69289.977184: bpf_trace_printk: match on tproxy_ip=7f000001
           <idle>-0       [001] dNs.. 69289.977185: bpf_trace_printk: forwarding_to_tproxy_port=58997
           <idle>-0       [001] dNs.. 69289.977187: bpf_trace_printk: Assigned

  
  
