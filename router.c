#include <arpa/inet.h> /* ntoh, hton and inet_ functions */
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "lib.h"
#include "protocols.h"
#include <string.h>

/* Routing table */
struct route_table_entry *rtable;
int rtable_len;

/* Arp table */
struct arp_table_entry *mac_table;
int mac_table_len;

/*
 Returns a pointer (eg. &rtable[i]) to the best matching route, or NULL if there
 is no matching route.
*/
struct route_table_entry *get_best_route(uint32_t ip_dest) {
	/* TODO 2.2: Implement the LPM algorithm */
	/* We can iterate through rtable for (int i = 0; i < rtable_len; i++). Entries in
	 * the rtable are in network order already */
	struct route_table_entry *newEntry = NULL;
	for (int i = 0; i < rtable_len; i++) {
    if (rtable[i].prefix == (ip_dest & rtable[i].mask)) {
      if (newEntry == NULL)
		newEntry = &rtable[i];
	if (ntohl(rtable[i].mask) > ntohl(newEntry->mask))
		newEntry = &rtable[i];
    }
}
	return newEntry;
}

struct arp_table_entry *get_mac_entry(uint32_t given_ip) {
	/* TODO 2.4: Iterate through the MAC table and search for an entry
	 * that matches given_ip. */

	/* We can iterate thrpigh the mac_table for (int i = 0; i <
	 * mac_table_len; i++) */
	for (int i = 0 ; i < mac_table_len; i++) {
		if (mac_table[i].ip == given_ip)
		return &mac_table[i];
	}
	return NULL;
}

int main(int argc, char *argv[])
{
	int interface;
	char packet[1500];
	int packet_len;

	/* Don't touch this */
	init(argv + 2, argc - 2);

	/* Code to allocate the MAC and route tables */
	rtable = malloc(sizeof(struct route_table_entry) * 1000000);
	/* DIE is a macro for sanity checks */
	DIE(rtable == NULL, "memory");

	mac_table = malloc(sizeof(struct  arp_table_entry) * 1000000);
	DIE(mac_table == NULL, "memory");
	
	/* Read the static routing table and the MAC table */
	rtable_len = read_rtable(argv[1], rtable);

	mac_table_len = parse_arp_table("arp_table.txt", mac_table);

	while (1) {
		/* We call get_packet to receive a packet. get_packet returns
		the interface it has received the data from. And writes to
		len the size of the packet. */
		interface = recv_from_any_link(packet, (size_t *)&packet_len);
		DIE(interface < 0, "get_message");
		printf("We have received a packet\n");
		
		/* Extract the Ethernet header from the packet. Since protocols are
		 * stacked, the first header is the ethernet header, the next header is
		 * at m.payload + sizeof(struct ether_header) */
		struct ether_hdr *eth_hdr = (struct ether_hdr *) packet;
		struct ip_hdr *ip_header = (struct ip_hdr *)(packet + sizeof(struct ether_hdr));

		/* Check if we got an IPv4 packet */
		if (eth_hdr->ethr_type != ntohs(ETHERTYPE_IP)) {
			printf("Ignored non-IPv4 packet\n");
			continue;
		}

		/* TODO 2.1: Check the ip_hdr integrity using ip_checksum((uint16_t *)ip_hdr, sizeof(struct iphdr)) */
		if (checksum((uint16_t *)ip_header, sizeof(struct ip_hdr)) != 0) {
			printf("Wrong IP!\n");
			continue;
		}
		/* TODO 2.2: Call get_best_route to find the most specific route, continue; (drop) if null */
		struct route_table_entry *bestRoute = get_best_route(ip_header->dest_addr);
		if (bestRoute == NULL) {
			printf("Route not found!\n");
			continue;
		}
		/* TODO 2.3: Check TTL > 1. Update TLL. Update checksum  */
		if (ip_header->ttl <= 1) {
			printf("TTL terminated!\n");
			continue;
		}
		ip_header->ttl--;
		ip_header->checksum = 0;
		ip_header->checksum = htonl(checksum((uint16_t *)ip_header, sizeof(struct ip_hdr)));
		/* TODO 2.4: Update the ethernet addresses. Use get_mac_entry to find the destination MAC
		 * address. Use get_interface_mac(m.interface, uint8_t *mac) to
		 * find the mac address of our interface. */
		struct arp_table_entry *mac = get_mac_entry(bestRoute->next_hop);
		if (mac == NULL) {
			printf("Wrong MAC!\n");
			continue;
		}
		memcpy(eth_hdr->ethr_dhost, mac->mac, 6);

		uint8_t macAddress[6];
		get_interface_mac(bestRoute->interface, macAddress);
		memcpy(eth_hdr->ethr_shost, macAddress, 6);
		// Call send_to_link(best_router->interface, packet, packet_len);
		send_to_link(packet_len, packet, bestRoute->interface);
	}
}