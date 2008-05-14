/* sock.c
** libstrophe XMPP client library -- socket abstraction implementation
**
** Copyright (C) 2005 OGG, LCC. All rights reserved.
**
**  This software is provided AS-IS with no warranty, either express
**  or implied.
**
**  This software is distributed under license and may not be copied,
**  modified or distributed except as expressly authorized under the
**  terms of the license contained in the file LICENSE.txt in this
**  distribution.
*/

#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windns.h>
#include <Iphlpapi.h>
#define snprintf _snprintf
#else
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <fcntl.h>
#endif

#include "sock.h"

void sock_initialize(void)
{
#ifdef _WIN32
    WSADATA wsad;
    WSAStartup(0x0101, &wsad);
#endif
}

void sock_shutdown(void)
{
#ifdef _WIN32
    WSACleanup();
#endif
}

int sock_error(void)
{
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

static int _in_progress(int error)
{
#ifdef _WIN32
    return (error == WSAEWOULDBLOCK || error == WSAEINPROGRESS);
#else
    return (errno == EINPROGRESS);
#endif
}

sock_t sock_connect(const char * const host, const unsigned int port)
{
    sock_t sock;
    char service[6];
    struct addrinfo *res, *ainfo, hints;
    int err;
    
    sock = -1;

    snprintf(service, 6, "%u", port);

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_socktype = SOCK_STREAM;

    if ((err = getaddrinfo(host, service, &hints, &res)) != 0)
	return -1;

    ainfo = res;
    while (ainfo) {
	if ((sock = socket(ainfo->ai_family, ainfo->ai_socktype, 
		   ainfo->ai_protocol)) >= 0) {
	    sock_set_nonblocking(sock);

	    err = connect(sock, ainfo->ai_addr, ainfo->ai_addrlen);

	    if ((err == 0) || (err < 0 && _in_progress(sock_error())))
		break;
	}

	ainfo = ainfo->ai_next;
    }

    if (res) freeaddrinfo(res);

    return sock;
}

int sock_close(const sock_t sock)
{
#ifdef _WIN32
    return closesocket(sock);
#else
    return close(sock);
#endif
}

int sock_set_blocking(const sock_t sock)
{
#ifdef _WIN32
    u_long block = 0;
    return ioctlsocket(sock, FIONBIO, &block);
#else
    return fcntl(sock, F_SETFL, 0);
#endif
}

int sock_set_nonblocking(const sock_t sock)
{
#ifdef _WIN32
    u_long nonblock = 1;
    return ioctlsocket(sock, FIONBIO, &nonblock);
#else
    return fcntl(sock, F_SETFL, O_NONBLOCK);
#endif
}

int sock_read(const sock_t sock, void * const buff, const size_t len)
{
    return recv(sock, buff, len, 0);
}

int sock_write(const sock_t sock, const void * const buff, const size_t len)
{
    return send(sock, buff, len, 0);
}

int sock_is_recoverable(const int error)
{
#ifdef _WIN32
    return (error == WSAEINTR || error == WSAEWOULDBLOCK || 
	    error == WSAEINPROGRESS);
#else
    return (error == EAGAIN || error == EINTR);
#endif
}

int sock_connect_error(const sock_t sock)
{
    struct sockaddr sa;
    int len;
    char temp;

    sa.sa_family = AF_INET;

    len = sizeof(sa);

    /* we don't actually care about the peer name, we're just checking if
     * we're connected or not */
    if (getpeername(sock, &sa, &len) == 0)
    {
	return 0;
    }

    /* it's possible that the error wasn't ENOTCONN, so if it wasn't,
     * return that */
#ifdef _WIN32
    if (sock_error() != WSAENOTCONN) return sock_error();
#else
    if (sock_error() != ENOTCONN) return sock_error();
#endif

    /* load the correct error into errno through error slippage */
    recv(sock, &temp, 1, 0);

    return sock_error();
}

struct dnsquery_header
{
	unsigned short id;
	unsigned char qr;
	unsigned char opcode;
	unsigned char aa;
	unsigned char tc;
	unsigned char rd;
	unsigned char ra;
	unsigned char z;
	unsigned char rcode;
	unsigned short qdcount;
	unsigned short ancount;
	unsigned short nscount;
	unsigned short arcount;
};

struct dnsquery_question
{
	char *qname;
	unsigned short qtype;
	unsigned short qclass;
};

struct dnsquery_resourcerecord
{
	char *name;
	unsigned short type;
	unsigned short _class;
	unsigned int ttl;
	unsigned short rdlength;
	void *rdata;
};

struct dnsquery_srvrdata
{
	unsigned short priority;
	unsigned short weight;
	unsigned short port;
	char *target;
};

void netbuf_add_32bitnum(unsigned char *buf, int buflen, int *offset, unsigned int num)
{
	unsigned char *start = buf + *offset;
	unsigned char *p = start;

	/* assuming big endian */
	*p++ = (num >> 24) & 0xff;
	*p++ = (num >> 16) & 0xff;
	*p++ = (num >> 8)  & 0xff;
	*p++ = (num)       & 0xff;

	*offset += 4;
}

void netbuf_get_32bitnum(unsigned char *buf, int buflen, int *offset, unsigned int *num)
{
	unsigned char *start = buf + *offset;
	unsigned char *p = start;
	*num = 0;

	/* assuming big endian */
	*num |= (*p++) << 24;
	*num |= (*p++) << 16;
	*num |= (*p++) << 8;
	*num |= (*p++);

	*offset += 4;
}

void netbuf_add_16bitnum(unsigned char *buf, int buflen, int *offset, unsigned short num)
{
	unsigned char *start = buf + *offset;
	unsigned char *p = start;

	/* assuming big endian */
	*p++ = (num >> 8) & 0xff;
	*p++ = (num)      & 0xff;

	*offset += 2;
}

void netbuf_get_16bitnum(unsigned char *buf, int buflen, int *offset, unsigned short *num)
{
	unsigned char *start = buf + *offset;
	unsigned char *p = start;
	*num = 0;

	/* assuming big endian */
	*num |= (*p++) << 8;
	*num |= (*p++);

	*offset += 2;
}

void netbuf_add_domain_name(unsigned char *buf, int buflen, int *offset, char *name)
{
	unsigned char *start = buf + *offset;
	unsigned char *p = start;
	unsigned char *wordstart, *wordend;

	wordstart = name;
	
	while (*wordstart)
	{
		int len;
		wordend = wordstart;
		while (*wordend && *wordend != '.')
		{
			wordend++;
		}

		len = (int)(wordend - wordstart);

		if (len > 0x3F)
		{
			len = 0x3F;
		}

		*p++ = len;

		while (wordstart != wordend)
		{
			*p++ = *wordstart++;
		}

		if (*wordstart == '.')
		{
			wordstart++;
		}
	}

	*p++ = '\0';

	*offset += p - start;
#if 0
	unsigned char *start = buf + *offset;
	unsigned char *p = start;

	while (*labellist)
	{
		int len = strlen(*labellist);
		char *p2 = *labellist;

		if (len > 0x3F)
		{
			len = 0x3F;
		}

		*p++ = len & 0xFF;
		while (len)
		{
			*p++ = *p2++;
			len--;
		}

		labellist++;
	}

	*p++ = '\0';

	*offset += p - start;
#endif
}

int calc_domain_name_size(unsigned char *buf, int buflen, int offset)
{
	unsigned char *p = buf + offset;
	int len = 0;

	while (*p)
	{
		if ((*p & 0xC0) == 0xC0)
		{
			int newoffset = 0;
			newoffset |= (*p++ & 0x3F) << 8;
			newoffset |= *p;

			p = buf + newoffset;
		}
		else
		{
			if (len)
			{
				len += 1;
			}
			len += *p;
			p += *p + 1;
		}
	}

	return len;
}

void netbuf_get_domain_name(unsigned char *buf, int buflen, int *offset, char **name)
{
	unsigned char *start = buf + *offset;
	unsigned char *p = start;
	char *p2;
	int *curroffset = offset;

	*name = malloc(sizeof(**name) * (calc_domain_name_size(buf, buflen, *offset) + 1));
	**name = '\0';

	while (*p)
	{
		if ((*p & 0xC0) == 0xC0)
		{
			int newoffset = 0;
			newoffset |= (*p++ & 0x3F) << 8;
			newoffset |= *p++;

			if (curroffset)
			{
				*curroffset += (int)(p - start);
				curroffset = NULL;
			}

			p = buf + newoffset;
		}
		else
		{
			if (**name != '\0')
			{
				strcat(*name, ".");
			}

			strncat(*name, p + 1, *p);
			p += *p + 1;
		}
	}

	if (curroffset)
	{
		p++;
		*curroffset += (int)(p - start);
		curroffset = NULL;
	}

#if 0
	unsigned char *start = buf + *offset;
	unsigned char *p = start;
	char **labelp;
	int numlabels = 1;
	int tracking = 1;

	/* check if this is a reference */
	if ((*p & 0xC0) == 0xC0)
	{
		/* it is, parse the old reference */
		int newoffset = 0;

		newoffset |= (*p++ & 0x3F) << 8;
		newoffset |= (*p++);

		netbuf_get_label_list(buf, buflen, &newoffset, labellist);
		*offset += 2;
		return;
	}

	/* count the number of labels we need */
	while (*p && )
	{
		if ((*p & 0xC0) == 0xC0)
		{
			p += 2;
		}
		else
		{
                        p += *p;
		}
		numlabels++;
	}

	*labellist = malloc(sizeof(**labellist) + 1);
	labelp = *labellist;

	p = start;

	while (*p)
	{
		if ((*p & 0xC0) == 0xC0)
		{
		}
		else
		{
			int len = *p++;
			unsigned char *p2;

			*labelp = malloc(len + 1);
			p2 = *labelp;

			while (len)
			{
				*p2++ = *p++;
				len--;
			}

			*p2++ = '\0';

			labelp++;
		}
	}

	*labelp = NULL;

	p++;

	*offset += p - start;
#endif
}

void netbuf_add_dnsquery_header(unsigned char *buf, int buflen, int *offset, struct dnsquery_header *header)
{
	unsigned char *p;

	netbuf_add_16bitnum(buf, buflen, offset, header->id);
	
	p = buf + *offset;
	*p++ =    ((header->qr     & 0x01) << 7)
		| ((header->opcode & 0x0F) << 3)
		| ((header->aa     & 0x01) << 2)
		| ((header->tc     & 0x01) << 1)
		| ((header->rd     & 0x01));
	*p++ =    ((header->ra     & 0x01) << 7)
		| ((header->z      & 0x07) << 4)
		| ((header->rcode  & 0x0F));
	*offset += 2;

	netbuf_add_16bitnum(buf, buflen, offset, header->qdcount);
	netbuf_add_16bitnum(buf, buflen, offset, header->ancount);
	netbuf_add_16bitnum(buf, buflen, offset, header->nscount);
	netbuf_add_16bitnum(buf, buflen, offset, header->arcount);
}

void netbuf_get_dnsquery_header(unsigned char *buf, int buflen, int *offset, struct dnsquery_header *header)
{
	unsigned char *p;
		
	netbuf_get_16bitnum(buf, buflen, offset, &(header->id));

	p = buf + *offset;
	header->qr =     (*p >> 7) & 0x01;
	header->opcode = (*p >> 3) & 0x0F;
	header->aa =     (*p >> 2) & 0x01;
	header->tc =     (*p >> 1) & 0x01;
	header->rd =     (*p)      & 0x01;
	p++;
	header->ra =     (*p >> 7) & 0x01;
	header->z =      (*p >> 4) & 0x07;
	header->rcode =  (*p)      & 0x0F;
	p++;
	*offset += 2;

	netbuf_get_16bitnum(buf, buflen, offset, &(header->qdcount));
	netbuf_get_16bitnum(buf, buflen, offset, &(header->ancount));
	netbuf_get_16bitnum(buf, buflen, offset, &(header->nscount));
	netbuf_get_16bitnum(buf, buflen, offset, &(header->arcount));
}

void netbuf_add_dnsquery_question(unsigned char *buf, int buflen, int *offset, struct dnsquery_question *question)
{
	/*netbuf_add_label_list(buf, buflen, offset, question->qname);*/
	netbuf_add_domain_name(buf, buflen, offset, question->qname);
	netbuf_add_16bitnum(buf, buflen, offset, question->qtype);
	netbuf_add_16bitnum(buf, buflen, offset, question->qclass);
}

void netbuf_get_dnsquery_question(unsigned char *buf, int buflen, int *offset, struct dnsquery_question *question)
{
	/*netbuf_get_label_list(buf, buflen, offset, &(question->qname));*/
	netbuf_get_domain_name(buf, buflen, offset, &(question->qname));
	netbuf_get_16bitnum(buf, buflen, offset, &(question->qtype));
	netbuf_get_16bitnum(buf, buflen, offset, &(question->qclass));
}

void netbuf_get_dnsquery_srvrdata(unsigned char *buf, int buflen, int *offset, struct dnsquery_srvrdata *srvrdata)
{
	netbuf_get_16bitnum(buf, buflen, offset, &(srvrdata->priority));
	netbuf_get_16bitnum(buf, buflen, offset, &(srvrdata->weight));
	netbuf_get_16bitnum(buf, buflen, offset, &(srvrdata->port));
	netbuf_get_domain_name(buf, buflen, offset, &(srvrdata->target));
}

void netbuf_get_dnsquery_resourcerecord(unsigned char *buf, int buflen, int *offset, struct dnsquery_resourcerecord *rr)
{
	/*netbuf_get_label_list(buf, buflen, offset, &(rr->name));*/
	netbuf_get_domain_name(buf, buflen, offset, &(rr->name));
	netbuf_get_16bitnum(buf, buflen, offset, &(rr->type));
	netbuf_get_16bitnum(buf, buflen, offset, &(rr->_class));
	netbuf_get_32bitnum(buf, buflen, offset, &(rr->ttl));
	netbuf_get_16bitnum(buf, buflen, offset, &(rr->rdlength));
	if (rr->type == 33) /* SRV */
	{
		int newoffset = *offset;
		rr->rdata = malloc(sizeof(struct dnsquery_srvrdata));
		netbuf_get_dnsquery_srvrdata(buf, buflen, &newoffset, rr->rdata);
	}
	else
	{
                rr->rdata = buf + *offset;
	}
	*offset += rr->rdlength;
}

char **SeparateStringByDots(char *string)
{
	FILE *fp;
	char **result;
	char *p = string;
	char **ps;

	int numstrings = 1;
	while (*p)
	{
		if (*p++ == '.')
		{
			numstrings++;
		}
	}

	p = string;

	result = malloc(sizeof(*result) * numstrings);
	ps = result;

	while (*p)
	{
		char *p2 = p;

		while (*p2 && *p2 != '.')
		{
			p2++;
		}

		*ps = malloc(sizeof(*ps) * ((int)(p2 - p) + 1));

		p2 = *ps;

		while (*p && *p != '.')
		{
			*p2++ = *p++;
		}

		*p2 = '\0';

		ps++;

		if (*p == '.')
		{
			p++;
		}
	}

	*ps = NULL;

	return result;
}

int sock_srv_lookup(const char *service, const char *proto, const char *domain, char *resulttarget, int resulttargetlength, int *resultport)
{
    int set = 0;
    char fulldomain[2048];

    snprintf(fulldomain, 2048, "_%s._%s.%s", service, proto, domain);
#ifdef _WIN32

    /* try using dnsapi first */
    if (!set)
    {
        HINSTANCE hdnsapi = NULL;
	
	DNS_STATUS (WINAPI * pDnsQuery_A)(PCSTR, WORD, DWORD, PIP4_ARRAY, PDNS_RECORD*, PVOID*);
	void (WINAPI * pDnsRecordListFree)(PDNS_RECORD, DNS_FREE_TYPE);

	if (hdnsapi = LoadLibrary("dnsapi.dll")) {

	    pDnsQuery_A = (void *)GetProcAddress(hdnsapi, "DnsQuery_A");
	    pDnsRecordListFree = (void *)GetProcAddress(hdnsapi, "DnsRecordListFree");

	    if (pDnsQuery_A && pDnsRecordListFree) {
		PDNS_RECORD dnsrecords = NULL;
		DNS_STATUS error;

		error = pDnsQuery_A(fulldomain, DNS_TYPE_SRV, DNS_QUERY_STANDARD, NULL, &dnsrecords, NULL);

		if (error == 0) {
		    PDNS_RECORD current = dnsrecords;

		    while (current) {
			if (current->wType == DNS_TYPE_SRV) {
			    snprintf(resulttarget, resulttargetlength, current->Data.Srv.pNameTarget);
			    *resultport = current->Data.Srv.wPort;
			    set = 1;

			    current = NULL;
			} else {
			    current = current->pNext;
			}
		    }
		}

		pDnsRecordListFree(dnsrecords, DnsFreeRecordList);
	    }
	    
	    FreeLibrary(hdnsapi);
	}
    }

    /* if dnsapi didn't work/isn't there, try querying the dns server manually */
    if (!set)
    {
	unsigned char buf[65536];
	struct dnsquery_header header;
	struct dnsquery_question question;
	unsigned char *p, **qname;
	int offset = 0;
	int addrlen;
	sock_t sock;
	struct sockaddr_in dnsaddr;
	char dnsserverips[16][256];
	int numdnsservers = 0;
	int i, j;

	/* Try getting the DNS server ips from GetNetworkParams() in iphlpapi first */
	if (!numdnsservers)
	{
		HINSTANCE hiphlpapi = NULL;
		DWORD (WINAPI * pGetNetworkParams)(PFIXED_INFO, PULONG);

		if (hiphlpapi = LoadLibrary("Iphlpapi.dll"))
		{
			pGetNetworkParams = (void *)GetProcAddress(hiphlpapi, "GetNetworkParams");

			if (pGetNetworkParams)
			{
				FIXED_INFO *fi;
				ULONG len;
				DWORD error;
				len = 0;

				/* get the size and malloc it */
				if ((error = pGetNetworkParams(NULL, &len)) == ERROR_BUFFER_OVERFLOW)
				{
					fi = malloc(len);
                                        if ((error = pGetNetworkParams(fi, &len)) == ERROR_SUCCESS)
					{
						IP_ADDR_STRING *pias = &(fi->DnsServerList);

						while (pias && numdnsservers < 16)
						{
                                                        strcpy(dnsserverips[numdnsservers++], pias->IpAddress.String);
							pias = pias->Next;
						}
					}
					free(fi);
				}
			}
		}
		FreeLibrary(hiphlpapi);
	}

	/* Next, try getting the DNS server ips from the registry */
	if (!numdnsservers)
	{
		HKEY search;
		LONG error;

		error = RegOpenKeyEx(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters", 0, KEY_READ, &search);

		if (error != ERROR_SUCCESS)
		{
			error = RegOpenKeyEx(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Services\\VxD\\MSTCP", 0, KEY_READ, &search);
		}

		if (error == ERROR_SUCCESS)
		{
			char name[512];
			DWORD len = 512;

			error = RegQueryValueEx(search, "NameServer", NULL, NULL, (LPBYTE)name, &len);

			if (error != ERROR_SUCCESS)
			{
				error = RegQueryValueEx(search, "DhcpNameServer", NULL, NULL, (LPBYTE)name, &len);
			}
		
			if (error == ERROR_SUCCESS)
			{
				char *parse = "0123456789.", *start, *end;
				start = name;
				end = name;
				name[len] = '\0';

				while (*start && numdnsservers < 16)
				{
					while (strchr(parse, *end))
					{
						end++;
					}

					strncpy(dnsserverips[numdnsservers++], start, end - start);

					while (*end && !strchr(parse, *end))
					{
						end++;
					}

					start = end;
				}
			}
		}

		RegCloseKey(search);
	}

	if (!numdnsservers)
	{
		HKEY searchlist;
		LONG error;

		error = RegOpenKeyEx(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Services\\Tcpip\\Parameters\\Interfaces", 0, KEY_READ, &searchlist);

		if (error == ERROR_SUCCESS)
		{
			int i;
			DWORD numinterfaces = 0;

			RegQueryInfoKey(searchlist, NULL, NULL, NULL, &numinterfaces, NULL, NULL, NULL, NULL, NULL, NULL, NULL);

			for (i = 0; i < numinterfaces; i++)
			{
				char name[512];
				DWORD len = 512;
				HKEY searchentry;

				RegEnumKeyEx(searchlist, i, (LPTSTR)name, &len, NULL, NULL, NULL, NULL);

				if (RegOpenKeyEx(searchlist, name, 0, KEY_READ, &searchentry) == ERROR_SUCCESS)
				{
					if (RegQueryValueEx(searchentry, "DhcpNameServer", NULL, NULL, (LPBYTE)name, &len) == ERROR_SUCCESS)
					{
						char *parse = "0123456789.", *start, *end;
						start = name;
						end = name;
						name[len] = '\0';

						while (*start && numdnsservers < 16)
						{
							while (strchr(parse, *end))
							{
								end++;
							}

							strncpy(dnsserverips[numdnsservers++], start, end - start);

							while (*end && !strchr(parse, *end))
							{
								end++;
							}

							start = end;
						}
					}
					else if (RegQueryValueEx(searchentry, "NameServer", NULL, NULL, (LPBYTE)name, &len) == ERROR_SUCCESS)
					{
						char *parse = "0123456789.", *start, *end;
						start = name;
						end = name;
						name[len] = '\0';

						while (*start && numdnsservers < 16)
						{
							while (strchr(parse, *end))
							{
								end++;
							}

							strncpy(dnsserverips[numdnsservers++], start, end - start);

							while (*end && !strchr(parse, *end))
							{
								end++;
							}

							start = end;
						}
					}
					RegCloseKey(searchentry);
				}
			}
			RegCloseKey(searchlist);
		}
	}

	/* If we have a DNS server, use it */
	if (numdnsservers)
	{
		ULONG nonblocking = 1;
		int i;
		int insize;

		memset(&header, 0, sizeof(header));
		header.id = 12345; /* FIXME: Get a better id here */
		header.rd = 1;
		header.qdcount = 1;

		netbuf_add_dnsquery_header(buf, 65536, &offset, &header);

		memset(&question, 0, sizeof(question));
		question.qname = fulldomain;
		question.qtype = 33; /* SRV */
		question.qclass = 1; /* INTERNET! */

		netbuf_add_dnsquery_question(buf, 65536, &offset, &question);

		insize = 0;
		for (i = 0; i < numdnsservers && insize <= 0; i++)
		{
			sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
			ioctlsocket(sock, FIONBIO, &nonblocking);

			memset(&dnsaddr, 0, sizeof(dnsaddr));

			dnsaddr.sin_family      = AF_INET;
			dnsaddr.sin_port        = htons(53);
			dnsaddr.sin_addr.s_addr = inet_addr(dnsserverips[i]);

			addrlen = sizeof(dnsaddr);
			sendto(sock, (char *)buf, offset, 0, (struct sockaddr *)&dnsaddr, addrlen);
			for (j = 0; j < 50; j++)
			{
				insize = recvfrom(sock, (char *)buf, 65536, 0, (struct sockaddr *)&dnsaddr, &addrlen);
				if (insize == SOCKET_ERROR)
				{
					if (sock_error() == WSAEWOULDBLOCK)
					{
						Sleep(100);
					}
					else
					{
						break;
					}
				}
				else
				{
					break;
				}
			}

			closesocket(sock);
		}

		offset = insize;

		if (offset > 0)
		{
			FILE *fp;

			int len = offset;
			int i;
			struct dnsquery_header header;
			struct dnsquery_question question;
			struct dnsquery_resourcerecord rr;

			offset = 0;
			netbuf_get_dnsquery_header(buf, 65536, &offset, &header);

			for (i = 0; i < header.qdcount; i++)
			{
				netbuf_get_dnsquery_question(buf, 65536, &offset, &question);
			}

			for (i = 0; i < header.ancount; i++)
			{
				netbuf_get_dnsquery_resourcerecord(buf, 65536, &offset, &rr);

				if (rr.type == 33)
				{
					struct dnsquery_srvrdata *srvrdata = rr.rdata;

					snprintf(resulttarget, resulttargetlength, srvrdata->target);
					*resultport = srvrdata->port;
					set = 1;
				}
			}

			for (i = 0; i < header.ancount; i++)
			{
				netbuf_get_dnsquery_resourcerecord(buf, 65536, &offset, &rr);
			}
		}
	}

    }

#else
#endif

    if (!set)
    {
	snprintf(resulttarget, resulttargetlength, domain);
	*resultport = 5222;
	return 0;
    }

    return 1;
}