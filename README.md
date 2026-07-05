Repository for the first homework of the Communication Networks class. In this homework
the students will implement the dataplane of a router.

Mai intai aloc tabelele ARP si MAC. Initializez lungimea tabelelor si adaug in arbore fiecare intrare din tabel.
Initializez coada de pachete si lungimea tabelei MAC cu 0. (Pentru ARP dinamic) 

Procesul de dirijare - Forwarding

Extrag header-ele Ethernet si IP.
Prima data verific daca checksum-ul header-ului IP este valid. Daca nu este valid, trebuie sa dau drop pachetului.
Apoi, verific daca pachetul este destinat router-ului(il tratez mai apoi ca echo_reply- pentru Protocolul ICMP).
Caut cea mai buna ruta folosind Longest Prefix Match(explicat mai jos). Verific daca TTL-ul pachetului a expirat.
In caz afirmativ, il tratez cu time_exceeded- pentru protoclul. Altfel, scad ttl-ul si reactualizez checksum-ul.
Caut MAC-ul urmatorului hop in tabela ARP. Daca nu il gasesc, adaug pachetul in coada de pachete si trimit arp_request.
Reactualizez destinatia si sursa MAC-ului si trimit pachetul pe interfata rutei gasite.

Longest Prefix Match - Eficient

Pentru a gasi cea mai buna ruta, am implementat LPM cu Trie, cum e sugerat in enuntul temei. Fiecare nod este cate un bit
din adresa IP, iar inserarea oricarei rute se face bit cu bit, in adancime. Mai intai, am initializat radacina cu ajutorul
functiei add_node, ce creaza un nod nou. Pentru functia de insert, parcurg bitii invers, de la MSB la LSB. Daca gasesc un
bit de 0 in masca, atunci ma opresc. Parcurg in continuare bitii din adresa IP. Daca nu exista nod la bitul curent, atunci
inseamna ca nu exista nodul si il vom crea cu ajutorul functiei add_node(). Daca exista nod la bitul curent, atunci coboram
in adancime. Functia de search cauta cea mai buna ruta. Daca gasesc un nod NULL sau am parcurs toti bitii, atunci returnez
cea mai buna ruta curenta. Altfel, atribui celei mai bune, cea mai buna ruta a nodului curent. Apelez recursiv functia,
parcurgand invers bitii. Functia get_best_route returneaza cea mai buna ruta.In main, initializez arborele parcurgand
intrarile din tabela de intrare ARP.

Explicatie Pimul Screenshot- Subiectul 1

In screenshot-ul "Subiectul1.png" am aratat functionarea procesului de dirijare. Host1, Host2 si Host3 trimit comenzi ping
si primesc raspunsuri in router1("We recieved a packet!"), ceea ce confirma functionalitatea forwarding-ului: pachetele
ajung la destinatie. Se poate observa ca pachetele sunt capturate si in Wireshark.

Protoclul ARP

Am implementat protocolul ARP dinamic eliminand fisierul arp_table.txt si folosindu-ma de cele trei functii create:
send_arp_reply, send_arp_request, add_in_queue.

Functia send_arp_reply raspunde cu adresa MAC a interfetei curente atunci cand router-ul primeste un ARP Request. Functia
extrage header-ele din pachetul curent si copiaza intr-un buffer toate datele pachetului aferent. Sunt extrase din nou
header-ele Ethernet si ARP din buffer-ul nou, pentru ca urmeaza ca o parte din informatii sa fie modificate. Sunt modificate
sursa si destinatia pentru Ethernet. In protocolul ARP, setez opcode la 2(corespunzator reply-ului), sender-ul hardware devine
MAC-ul, sender protocol address devine IP-ul, iar target hardware devine adresa sender-ului din ce primim. Apelez aceasta
functie cand primesc un pachet cu Opcode 1.

Functia send_arp_request trimite un ARP request atunci cand router-ul trebuie sa dirijeze un pachet in care nu e cunoscut
MAC-ul urmatorului hop. Pachetul trebuie creat de la 0, astfel ca nu mai copiez nimic, ci setez buffer-ul initial la 0.
Extrag header-ele Ethernet si ARP, in care face aproximativ acelasi lucru ca la arp_reply. Pentru ARP, setez si restul
campurilor conform RFC 5342(mentionat in enunt). Apelez aceasta cand nu gasesc MAC-ul urmatorului hop.

Functia add_in_queue creeaza coada de asteptare a pachetelor. Acest lucru este important pentru ca nu vreau ca router-ul sa
se blocheze. Functia adauga o noua intrare in tabela ARP, apoi initializeaza o noua coada de pachete. Parcurg coada initiala
si pentru fiecare pachet verific daca next hop este acelasi cu ip-ul primit. Daca este, atunci actualizez campul Ethernet si
trimit pachetul pe interfata corespunzatoare. Daca nu este, atunci il adaug in coada de asteptare. La sfarsit, mut toate
elementele din coada creata mai devreme in coada principala. Apelez aceasta functie cand primesc un pachet cu opcode 2.
Astfel, trimit pachetele care asteptau in coada, pentru ca am primit un raspuns la request.

Explicatie Al Doilea Screenshot- Subiectul 2

In screenshot-ul "Subiectul2.png" am aratat functionarea protocolului ARP. Router-ul primeste pachete, dar pentru unele nu
gaseste MAC-ul. (Pentru debug, am printat si cateva mesaje in functiile send_arp_reply si add_in_queue, pe care ulterior
le-am sters). Wireshark arata schimbul de "raspunsuri": ARP_Request intreaba catre cine e destinat pachetul, iar ARP_Reply
raspunde. Apar si alte pachete care sunt procesate normal, cu ICMP.

Protocolul ICMP

Am implementat cele trei tipuri de mesaje mentionate in enunt: Echo Reply, Destination Unreachable, Time Exceeded.

Functia send_echo_reply retine header-ele Ethernet, IP, ICMP din pachetul curent, copiaza datele din pachetul curent intr-un
buffer si apoi modifica cele trei headere. Pentru Ethernet, este nevoie sa modificam destinatarul si sursa. Pentru IP, se schimba
sursa cu destinatia, TTL-ul este resetat(l-am resetat la 100) si checksum-ul este reactualizat abia la final, dupa ce fac
modificarile aferente. Pentru ICMP, type-ul si codul devin 0(mentionat in enunt). Apelez functia atunci cand roueter-ul primeste
un ICMP destinat lui. Este comparat IP-ul interfetei cu IP-ul destinatie.

Functia send_destination_unreachable extrage aceleasi 3 headere ca la send_echo_reply. Facem aproximativ aceleasi modificari,
dar la nivelul ICMP extragem si payload-ul(8 bytes -> 64 biti), iar la nivelul IP modificam type si code. Recalculam checksum-ul
dar si lungimea totala a pachetului (adaugam payload-ul). Apelez functia atunci cand nu exista nicio ruta in tabela de rutare catre destinatie.

Functia send_time_exceeded extrage aceleasi 3 headere ca la send_echo_reply. Facem aceleasi modificari ca la
send_destination_unreachable, doar ca modificam type si code-ul. Apelez functia atunci cand TTL a expirat
(in verificarea daca TTL < 1), adica pachetul a expirat si nu mai poate fi dirijat.

Explicatie Al Treilea Screenshot- Subiectul 3

In screenshot-ul "Subiectul3.png" am aratat functionarea protocolului ICMP. Exista 3 cazuri pe care le-am tratat in host-uri diferite.
Cazul 1: send_echo_reply(pe Host2) -> Host-ul trimite ping si primeste raspuns si 0% packet loss. Astfel, se trimite un Echo Reply.
Cazul 2: send_time_exceeded(pe Host0) -> se trimite un ping cu ttl = 1. Router-ul verifica pachetul in if-ul corespunzator
time-to-live si da drop la pachet, trimitand si mesajul de eroare "TTL Terminated!". Cazul 3: send_destination_unreachable(pe Host1).
Este trimis ping catre o adresa care nu exista in tabela de rutare. Este trimis un mesaj de eroare "Destination Unreachable!".
Pentru acest subiect, am lucrat cu ARP static.
