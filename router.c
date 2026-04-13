#include <arpa/inet.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include "lib.h"
#include "protocols.h"
#include <string.h>
#include "queue.h"

// Tabela de rutare
struct route_table_entry *rtable;
int rtable_len;

// Tabela ARP
struct arp_table_entry *mac_table;
int mac_table_len;

// Coada de pachete
queue my_queue;

// Pachet de cozi, contine pachete ce asteapta raspuns ARP
struct packet_queue {
	char packet[1500];
	int packet_len;
	int interface;
	uint32_t next_hop;
};

// Structura trie-ului, best route e ceea ce caut
struct trie_node {
	struct trie_node *children[2];
	struct route_table_entry *best_route;
};

// Radacina arborelui
struct trie_node *root;

struct trie_node *add_node() {
	struct trie_node *new_node = malloc(sizeof(struct trie_node));
	new_node->children[0] = NULL;
	new_node->children[1] = NULL;
	new_node->best_route = NULL;
	return new_node;
}

void insert_node(struct trie_node *root, struct route_table_entry *new_entry) {
	struct trie_node *current = root;
	// schimbam din network order in host order
	uint32_t ip = ntohl(new_entry->prefix);
	uint32_t mask = ntohl(new_entry->mask);

	// parcurgem invers bitii
	for (int i = 31; i >= 0; i--) {
		// extragem bitul i din masca
		int mask_bit = (mask >> i) & 1;
		// daca am gasit un bit de 0, ne oprim
		if (mask_bit == 0) {
			break;
		}
		// extragem bitul i din prefix
		int ip_bit = (ip >> i) & 1;
		// daca nu exista nod la bitul curent, cream noi un nou nod
		if (current->children[ip_bit] == NULL) {
			current->children[ip_bit] = add_node();
		}
		// daca exista, atunci coboram spre copilul bitului curent
		current = current->children[ip_bit];
	}
	// aici este best-route 
	current->best_route = new_entry;
}

struct route_table_entry *search(struct trie_node *root, struct route_table_entry *best_route, uint32_t ip_dest, int nr_bits) {
	// daca gasim un nod NULL, atunci nu exista ruta
	if (root == NULL || nr_bits < 0)
		return best_route;
	// altfel, verificam daca exista un best_route in nodul curent
	// daca exista, atunci il luam ca best_route actual
	if (root != NULL && root->best_route != NULL) {	
			best_route = root->best_route;
	}
	// coboram spre copilul bitului curent
	int ip_bit = (ip_dest >> nr_bits) & 1;
	// apelez recursiv functia
	return search(root->children[ip_bit], best_route, ip_dest, nr_bits - 1);
}

struct route_table_entry *get_best_route(uint32_t ip_dest) {
	// initializam best route, asta vom returna
	struct route_table_entry *best_route = NULL;
	// schimbam din network order in host order
	uint32_t ip = ntohl(ip_dest);
	// apelam functia de search
	best_route = search(root, best_route, ip, 31);
	return best_route;
}

struct arp_table_entry *get_mac_entry(uint32_t given_ip) {
	// parcurg tabela si caut intrarile cu IP-ul given_ip
	for (int i = 0 ; i < mac_table_len; i++) {
		// daca o gasesc, returnez un pointer la ea
		if (mac_table[i].ip == given_ip)
		return &mac_table[i];
	}
	return NULL;
}

void send_echo_reply(int packet_len, char *packet, int interface) {
	// iau headerele din pachetul curent
	struct ether_hdr *current_eth = (struct ether_hdr *)packet;
	struct ip_hdr *current_ip = (struct ip_hdr *)(packet + sizeof(struct ether_hdr));

	// copiez in buffer pachetul curent pentru ca urmeaza sa il modific
	char buf[1500];
	// retine informatiile pe care nu le reactualizez
	memcpy(buf, packet, packet_len);

	// iau headerele din pachetul nou
	struct ether_hdr *new_eth = (struct ether_hdr *)buf;
	struct ip_hdr *new_ip = (struct ip_hdr *)(buf + sizeof(struct ether_hdr));
	struct icmp_hdr *new_icmp = (struct icmp_hdr *)(buf + sizeof(struct ether_hdr) + sizeof(struct ip_hdr));

	// pentru Ethernet
	// schimb destinatarul
	memcpy(new_eth->ethr_dhost, current_eth->ethr_shost, 6);
	uint8_t macAddress[6];
	get_interface_mac(interface, macAddress);
	// sursa devine mac-ul interfetei pe care o voi trimite
	memcpy(new_eth->ethr_shost, macAddress, 6);

	// pentru ip
	// destinatia devine sursa
	new_ip->dest_addr = current_ip->source_addr;
	// new_ip->frag = se copiaza din pachetul curent
	// new_ip->id = se copiaza din pachetul curent
	// new_ip->ihl = se copiaza din pachetul curent
	// new_ip->proto = se copiaza din pachetul curent
	// sursa devine ip-ul interfetei noastre
	new_ip->source_addr = inet_addr(get_interface_ip(interface));
	// new_ip->tos = se copiaza din pachetul curent
	// new_ip->tot_len = se copiaza din pachetul curent
	// resetez ttl-ul
	new_ip->ttl = 100;
	// new_ip->ver = se copiaza din pachetul curent
	// recalculez checksum-ul cu noul ip
	// am schimbat ordinea, calculez checksum-ul dupa ce termin de modificat structura
	new_ip->checksum = 0;
	new_ip->checksum = htons(checksum((uint16_t *)new_ip, sizeof(struct ip_hdr)));

	// pentru icmp
	// echo_reply are codul (0,0)
	new_icmp->check = 0;
	new_icmp->mcode = 0;
	new_icmp->mtype = 0;

	// recalculez checksum pentru icmp
	new_icmp->check = htons(checksum((uint16_t *)new_icmp, sizeof(struct ip_hdr)));
	send_to_link(packet_len, buf, interface);
}
void send_destination_unreachable(int packet_len, char *packet, int interface) {
	// iau headerele din pachetul curent
	struct ether_hdr *current_eth = (struct ether_hdr *)packet;
	struct ip_hdr *current_ip = (struct ip_hdr *)(packet + sizeof(struct ether_hdr));

	// copiez in buffer pachetul curent pentru ca urmeaza sa il modific
	char buf[1500];
	// retine informatiile pe care nu le reactualizez
	memcpy(buf, packet, packet_len);

	// iau headerele din pachetul nou
	struct ether_hdr *new_eth = (struct ether_hdr *)buf;
	struct ip_hdr *new_ip = (struct ip_hdr *)(buf + sizeof(struct ether_hdr));
	struct icmp_hdr *new_icmp = (struct icmp_hdr *)(buf + sizeof(struct ether_hdr) + sizeof(struct ip_hdr));

	// pentru Ethernet
	// schimb destinatarul
	memcpy(new_eth->ethr_dhost, current_eth->ethr_shost, 6);
	uint8_t macAddress[6];
	get_interface_mac(interface, macAddress);
	// sursa devine mac-ul interfetei pe care o voi trimite
	memcpy(new_eth->ethr_shost, macAddress, 6);

	// pentru ip
	// recalculez checksum-ul cu noul ip
	// am schimbat ordinea, calculez checksum-ul dupa ce termin de modificat structura
	new_ip->checksum = 0;
	new_ip->checksum = htons(checksum((uint16_t *)new_ip, sizeof(struct ip_hdr)));
	// destinatia devine sursa
	new_ip->dest_addr = current_ip->source_addr;
	// new_ip->frag = se copiaza din pachetul curent
	// new_ip->id = se copiaza din pachetul curent
	// new_ip->ihl = se copiaza din pachetul curent
	// indiferent de tipul pahcetului, trebuie sa il declar ca pachet ICMP
	new_ip->proto = 1;
	// sursa devine ip-ul interfetei noastre
	new_ip->source_addr = inet_addr(get_interface_ip(interface));
	// new_ip->tos = se copiaza din pachetul curent
	// calculez lungimea header-ului din pachet(in bytes)
	int current_ip_len = current_ip->ihl * 4;
	// lungimea totala din icmp(payload-ul are 64 biti-> mai adaug 8 bytes)
	int total_len = sizeof(struct icmp_hdr) + current_ip_len + 8;
	// resetez lungimea totala, pe care tocmai am calculat-o
	// e nevoie de htons -> trebuie convertit in netowrk order
	new_ip->tot_len = htons(sizeof(struct ip_hdr) + total_len);
	// resetez ttl-ul
	new_ip->ttl = 100;
	// new_ip->ver = se copiaza din pachetul curent

	// pentru icmp
	// host_unreachable are codul (3, 0)
	new_icmp->check = 0;
	new_icmp->mcode = 0;
	new_icmp->mtype = 3;

	// extragem payload-ul din ICMP
	uint8_t *payload = (uint8_t *)new_icmp + sizeof(struct icmp_hdr);
	// copiez header-ul IP si apoi adaug payload-ul(care are 64 biti -> 8 bytes)
	memcpy(payload, current_ip, current_ip_len + 8);
	// recalculez checksum pentru icmp
	new_icmp->check = 0;
	new_icmp->check = htons(checksum((uint16_t *)new_icmp, total_len));
	// recalculez lungimea totala a pachetului
	int full = sizeof(struct ether_hdr) + ntohs(new_ip->tot_len);
	send_to_link(full, buf, interface);
}
void send_time_exceeded(int packet_len, char *packet, int interface) {
	// iau headerele din pachetul curent
	struct ether_hdr *current_eth = (struct ether_hdr *)packet;
	struct ip_hdr *current_ip = (struct ip_hdr *)(packet + sizeof(struct ether_hdr));

	// copiez in buffer pachetul curent pentru ca urmeaza sa il modific
	char buf[1500];
	// retine informatiile pe care nu le reactualizez
	memcpy(buf, packet, packet_len);

	// iau headerele din pachetul nou
	struct ether_hdr *new_eth = (struct ether_hdr *)buf;
	struct ip_hdr *new_ip = (struct ip_hdr *)(buf + sizeof(struct ether_hdr));
	struct icmp_hdr *new_icmp = (struct icmp_hdr *)(buf + sizeof(struct ether_hdr) + sizeof(struct ip_hdr));

	// pentru Ethernet
	// schimb destinatarul
	memcpy(new_eth->ethr_dhost, current_eth->ethr_shost, 6);
	uint8_t macAddress[6];
	get_interface_mac(interface, macAddress);
	// sursa devine mac-ul interfetei pe care o voi trimite
	memcpy(new_eth->ethr_shost, macAddress, 6);

	// pentru ip
	// destinatia devine sursa
	new_ip->dest_addr = current_ip->source_addr;
	// new_ip->frag = se copiaza din pachetul curent
	// new_ip->id = se copiaza din pachetul curent
	// new_ip->ihl = se copiaza din pachetul curent
	// indiferent de tipul pachetului, trebuie sa il declar ca pachet ICMP
	new_ip->proto = 1;
	// sursa devine ip-ul interfetei noastre
	new_ip->source_addr = inet_addr(get_interface_ip(interface));
	// new_ip->tos = se copiaza din pachetul curent
	// calculez lungimea header-ului din pachet(in bytes)
	int current_ip_len = current_ip->ihl * 4;
	// lungimea totala din icmp(payload-ul are 64 biti-> mai adaug 8 bytes)
	int total_len = sizeof(struct icmp_hdr) + current_ip_len + 8;
	// resetez lungimea totala, pe care tocmai am calculat-o
	// e nevoie de htons -> trebuie convertit in netowrk order
	new_ip->tot_len = htons(sizeof(struct ip_hdr) + total_len);
	// resetez ttl-ul
	new_ip->ttl = 100;
	// new_ip->ver = se copiaza din pachetul curent
	// recalculez checksum-ul cu noul ip
	// am schimbat ordinea, calculez checksum-ul dupa ce termin de modificat structura
	new_ip->checksum = 0;
	new_ip->checksum = htons(checksum((uint16_t *)new_ip, sizeof(struct ip_hdr)));

	// pentru icmp
	// time_exceeded are codul (11, 0)
	new_icmp->check = 0;
	new_icmp->mcode = 0;
	new_icmp->mtype = 11;

	// extragem payload-ul din ICMP
	uint8_t *payload = (uint8_t *)new_icmp + sizeof(struct icmp_hdr);
	// copiez header-ul IP si apoi adaug payload-ul(care are 64 biti -> 8 bytes)
	memcpy(payload, current_ip, current_ip_len + 8);
	// recalculez checksum pentru icmp
	new_icmp->check = 0;
	new_icmp->check = htons(checksum((uint16_t *)new_icmp, total_len));
	// recalculez lungimea totala a pachetului
	int full = sizeof(struct ether_hdr) + ntohs(new_ip->tot_len);
	send_to_link(full, buf, interface);
}
void send_arp_reply(int interface, char *packet, int packet_len) {
	// iau headerele din pachetul curent
	struct ether_hdr *current_eth = (struct ether_hdr *)packet;
	struct arp_hdr *current_arp = (struct arp_hdr *)(packet + sizeof(struct ether_hdr));

	// copiez in buffer pachetul curent pentru ca urmeaza sa il modific
	char buf[1500];
	// retine informatiile pe care nu le reactualizez
	memcpy(buf, packet, packet_len);

	// iau headerele din pachetul nou
	struct ether_hdr *new_eth = (struct ether_hdr *)buf;
	struct arp_hdr *new_arp = (struct arp_hdr *)(buf + sizeof(struct ether_hdr));

	// pentru Ethernet
	// schimb destinatarul
	memcpy(new_eth->ethr_dhost, current_eth->ethr_shost, 6);
	uint8_t macAddress[6];
	get_interface_mac(interface, macAddress);
	// sursa devine mac-ul interfetei pe care o voi trimite
	memcpy(new_eth->ethr_shost, macAddress, 6);
	new_eth->ethr_type = htons(ETHERTYPE_ARP);

	// pentru ARP
	// modific tipul de arp (2 este pentru reply)
	new_arp->opcode = htons(2);
	// sender-ul hardware devine adresa MAC de mai sus
	memcpy(new_arp->shwa, macAddress, 6);
	// sender-ul protocol devine interfata din argument
	new_arp->sprotoa = inet_addr(get_interface_ip(interface));
	// target-ul este sender-hardware(ca la dest-source)
	memcpy(new_arp->thwa, current_arp->shwa, 6);
	new_arp->tprotoa = current_arp->sprotoa;

	send_to_link(packet_len, buf, interface);
}
void send_arp_request(int interface, uint32_t next_hop) {
	// creez un nou buffer pentru ca nu am de unde sa copiez datele
	// trebuie sa creez eu de la 0 pachetul
	char buf[1500];
	// retine informatiile pe care nu le reactualizez
	memset(buf, 0, sizeof(buf));

	// iau headerele din pachetul nou
	struct ether_hdr *new_eth = (struct ether_hdr *)buf;
	struct arp_hdr *new_arp = (struct arp_hdr *)(buf + sizeof(struct ether_hdr));

	// pentru Ethernet
	// schimb destinatarul
	uint8_t dest[6];
	memset(dest, 0xFF, 6);
	memcpy(new_eth->ethr_dhost, dest, 6);
	uint8_t macAddress[6];
	get_interface_mac(interface, macAddress);
	// sursa devine mac-ul interfetei pe care o voi trimite
	memcpy(new_eth->ethr_shost, macAddress, 6);
	new_eth->ethr_type = htons(ETHERTYPE_ARP);

	// pentru ARP
	// lungimea adresei MAC
	new_arp->hw_len = 6;
	// Ethernet
	new_arp->hw_type = htons(1);
	// modific tipul operatiei de arp (1 este pentru request)
	new_arp->opcode = htons(1);
	// lungime adresa IPv4
	new_arp->proto_len = 4;
	// il facem IPv4
	new_arp->proto_type = htons(ETHERTYPE_IP);
	// sender-ul hardware devine adresa MAC de mai sus
	memcpy(new_arp->shwa, macAddress, 6);
	// sender-ul protocol devine interfata din argument
	new_arp->sprotoa = inet_addr(get_interface_ip(interface));
	// target-ul este next_hopul pentru care cautam MAC-ul
	uint8_t mac[6];
	memset(mac, 0, 6);
	memcpy(new_arp->thwa, mac, 6);
	// adresa IP pentru care cautam MAC
	new_arp->tprotoa = next_hop;

	send_to_link(sizeof(struct ether_hdr) + sizeof(struct arp_hdr), buf, interface);
}
void add_in_queue(struct arp_hdr *packet) {
	// adaugam o noua intrarea in tabela ARP
	mac_table[mac_table_len].ip = packet->sprotoa;
	memcpy(mac_table[mac_table_len].mac, packet->shwa, 6);
	mac_table_len++;

	// avem nevoie de o noua coada pentru a procesa pachetele carora nu vreau sa le dau reply
	queue packets = create_queue();
	// parcurgem coada si extragem elementele
	while (!queue_empty(my_queue)) {
		struct packet_queue *val = (struct packet_queue *)queue_deq(my_queue);
		// verificam daca am pachetul asteapta raspunsul
		if (val->next_hop == packet->sprotoa) {
			struct ether_hdr *new_eth = (struct ether_hdr *)val->packet;
			// pentru Ethernet- parcurgem ca la ICMP
			memcpy(new_eth->ethr_dhost, packet->shwa, 6);
			uint8_t macAddress[6];
			get_interface_mac(val->interface, macAddress);
			memcpy(new_eth->ethr_shost, macAddress, 6);
			send_to_link(val->packet_len, val->packet, val->interface);
		} else {
			// nu trimitem pachetul, il adaugam in coada nou creata
			queue_enq(packets, val);
		}
	}
	// punem toate elementele din coada packets in coada principala
	while (!queue_empty(packets)) {
		struct packet_queue *current = (struct packet_queue *)queue_deq(packets);
		queue_enq(my_queue, current);
	}
}
int main(int argc, char *argv[])
{
	int interface;
	char packet[1500];
	int packet_len;

	// creez arborele
	root = add_node();

	init(argv + 2, argc - 2);

	// aloc tabelele
	// aloc un numar mai mare, pentru a nu avea probleme cu memoria
	// rtable0.txt si rtable1.txt au dimensiuni mari
	rtable = malloc(sizeof(struct route_table_entry) * 1000000);
	DIE(rtable == NULL, "memory");

	mac_table = malloc(sizeof(struct  arp_table_entry) * 1000000);
	DIE(mac_table == NULL, "memory");
	
	//citesc tabelele de rutare
	rtable_len = read_rtable(argv[1], rtable);

	// inseram toate intrarile din tabela de rutare in arbore
	for (int i = 0; i < rtable_len; i++) {
		insert_node(root, &rtable[i]);
	}

	// PENTRU ARP DINAMIC
	// sterg fisierul arp_table.txt, scap de ARP static
	mac_table_len = 0;
	// in afara while-ului, pierdeam pachete la fiecare while
	my_queue = create_queue();

	while (1) {
		// astept primirea pachetelor pe orice interfata
		interface = recv_from_any_link(packet, (size_t *)&packet_len);
		DIE(interface < 0, "get_message");
		printf("We have received a packet\n");
		
		// extragem header-ele EThernet si IP
		struct ether_hdr *eth_hdr = (struct ether_hdr *) packet;
		struct ip_hdr *ip_header = (struct ip_hdr *)(packet + sizeof(struct ether_hdr));


		// verific daca este pachet ARP
		if (eth_hdr->ethr_type == htons(ETHERTYPE_ARP)) {
			// extrag ARP-ul din pachetul curent
			struct arp_hdr *arp_header = (struct arp_hdr *)(packet + sizeof(struct ether_hdr));
			// verific daca e de tip arp_request
			if (ntohs(arp_header->opcode) == 1) {
				send_arp_reply(interface, packet, packet_len);
			} else if (ntohs(arp_header->opcode) == 2) {
				// daca e de tip reply, adaugam in coada header-ul arp
				add_in_queue(arp_header);
			}
			continue;
		}

		// verific daca pachetul este IP. daca nu este, il ignor
		if (eth_hdr->ethr_type != ntohs(ETHERTYPE_IP)) {
			printf("Ignored non-IPv4 packet\n");
			continue;
		}

		// verific checksum-ul
		if (checksum((uint16_t *)ip_header, sizeof(struct ip_hdr)) != 0) {
			printf("Wrong IP!\n");
			continue;
		}

		// trebuie sa verific si daca pachetul este destinat router-ului
		// altfel, toate pachetele ICMP ar fi tratate ca Echo Request
		uint32_t current_ip = inet_addr(get_interface_ip(interface));
		if (ip_header->dest_addr == current_ip) {
			// verificam daca pachetul este de tip ICMP
			if (ip_header->proto == 1) {
					struct icmp_hdr *new_icmp = (struct icmp_hdr *)(packet + sizeof(struct ether_hdr) + sizeof(struct ip_hdr));
					if (new_icmp->mtype == 8 && new_icmp->mcode == 0) {
						printf("Transmitting ICMP Echo Request!\n");
						send_echo_reply(packet_len, packet, interface);
					}
			}
			continue;
		}

		// cautam cea mai buna ruta pentru destinatia pachetului
		// aici apelez la longest prefix match, facut cu Trie.
		struct route_table_entry *bestRoute = get_best_route(ip_header->dest_addr);
		// daca nu exista ruta, trimit mesajul de eroare
		if (bestRoute == NULL) {
			printf("Route not found!\n");
			send_destination_unreachable(packet_len, packet, interface);
			continue;
		}

		// verific daca a expirat TTL-ul si recalculez checksum-ul
		// daca se termina TTL, atunci trimit mesajul de eroare
		if (ip_header->ttl <= 1) {
			printf("TTL terminated!\n");
			send_time_exceeded(packet_len, packet, interface);
			continue;
		}

		ip_header->ttl--;
		ip_header->checksum = 0;
		// am modificat checksum pentru ca reteaua foloseste big-endian(network-order)
		// pe cand calculatorul foloseste little-endian(host-order)
		ip_header->checksum = htons(checksum((uint16_t *)ip_header, sizeof(struct ip_hdr)));
		
		// caut mac-ul urmatorului hop din tabela
		struct arp_table_entry *mac = get_mac_entry(bestRoute->next_hop);
		if (mac == NULL) {
			printf("Wrong MAC, adding in Queue!\n");
			// adaug in coada un nou pachet
			struct packet_queue *current_packet = malloc(sizeof(struct packet_queue));
			// initializez campurile cu cele din pachetul best_route
			current_packet->interface = bestRoute->interface;
			current_packet->next_hop = bestRoute->next_hop;
			current_packet->packet_len = packet_len;
			memcpy(current_packet->packet, packet, packet_len);
			queue_enq(my_queue, current_packet);
			// trimit ARP_Request
			send_arp_request(bestRoute->interface, bestRoute->next_hop);
			continue;
		}
		// actualizez MAC-ul destinatie cu MAC-ul urmatorului hop
		memcpy(eth_hdr->ethr_dhost, mac->mac, 6);
		// actualizez MAC-ul sursa cu MAC-ul interfetei
		uint8_t macAddress[6];
		get_interface_mac(bestRoute->interface, macAddress);
		memcpy(eth_hdr->ethr_shost, macAddress, 6);
		// trimit pachetul
		send_to_link(packet_len, packet, bestRoute->interface);
	}
}