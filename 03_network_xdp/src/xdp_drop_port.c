#include <linux/bpf.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/in.h>

// Macro for no bpf_helpers.h
#define SEC(NAME) __attribute__((section(NAME), used))
#define bpf_htons(x) __builtin_bswap16(x)

// Port to defense
#define TARGET_PORT 9999

SEC("xdp")
int xdp_drop_prog(struct xdp_md *ctx) {
    // 1. Get packet data and data_end pointers
    void *data_end = (void *)(long)ctx->data_end;
    void *data     = (void *)(long)ctx->data;

    // 2. Parse Ethernet header(MAC) and check if it's IPv4
    struct ethhdr *eth = data;
    if ((void *)(eth + 1) > data_end)
        return XDP_PASS; // Abnormal packet, pass it
    
    // Check if it's IPv4
    if (eth->h_proto != bpf_htons(ETH_P_IP))
        return XDP_PASS; // Not IPv4, pass it
    
    // 3. Parse IP header and check if it's UDP
    struct iphdr *ip = data + sizeof(struct ethhdr);
    if ((void *)(ip + 1) > data_end)
        return XDP_PASS; // Abnormal packet, pass it
    
    // Check if it's UDP
    if (ip->protocol != IPPROTO_UDP)
        return XDP_PASS; // Not UDP
    
    // 4. Parse UDP header and check destination port
    struct udphdr *udp = (void *)ip + (ip->ihl * 4);
    if ((void *)(udp + 1) > data_end)
        return XDP_PASS; // Abnormal packet, pass it
    
    // 5. If destination port matches TARGET_PORT, drop the packet
    if (udp->dest == bpf_htons(TARGET_PORT)) {
        // Drop packet if destination port matches TARGET_PORT
        return XDP_DROP;
    }

    return XDP_PASS; // Otherwise, pass the packet
}

char _license[] SEC("license") = "GPL";
