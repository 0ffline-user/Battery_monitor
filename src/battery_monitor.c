#include <sys/inotify.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <sys/epoll.h>
#include <linux/netlink.h>
#include <linux/netlink_diag.h>
#include <linux/sock_diag.h>
#include <linux/rtnetlink.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <systemd/sd-bus.h>
#include <linux/prctl.h>
#include <sys/prctl.h>
#include "config.h"

#ifdef DEBUG
	#define EC(l) ( (l) )
#else
	#define EC(l) ( 1 )	
#endif

static void sig_hand(int sig)
{
}

static __u32 uncover_magic_mgroups(void)
{
	int s = socket(AF_NETLINK, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT);
	if(s == -1)
	{
		return (1 << 1);
	}
	
	struct sockaddr_nl s_snl = { .nl_family = AF_NETLINK, .nl_groups = ~((__u32)0) };
	if(bind(s, (struct sockaddr*)&s_snl, sizeof(s_snl)) == -1)
	{
		close(s);
		return (1 << 1);
	}
	
	int nds = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_SOCK_DIAG);
	if(nds == -1)
	{
		close(s);
		return (1 << 1);
	}
	
	struct sockaddr_nl nds_snl = { .nl_family = AF_NETLINK };
	if(bind(nds, (struct sockaddr*)&nds_snl, sizeof(nds_snl)) == -1)
	{
		close(nds);
		close(s);
		return (1 << 1);
	}
	
	struct stat s_st = { 0 };
	if(fstat(s, &s_st) == -1)
	{
		close(nds);
		close(s);
		return (1 << 1);
	}

	struct {
		struct nlmsghdr nlh;
        	struct netlink_diag_req ndr;
	} req = {
		.nlh = {
			.nlmsg_len = sizeof(req),
            		.nlmsg_type = SOCK_DIAG_BY_FAMILY,
            		.nlmsg_flags = NLM_F_REQUEST
		},
		.ndr = {
			.sdiag_family = AF_NETLINK,
			.ndiag_show = NDIAG_SHOW_GROUPS,
			.ndiag_ino = (__u32)s_st.st_ino
		}
	};
	
	struct sockaddr_nl k_snl = { .nl_family = AF_NETLINK };
	struct iovec iov = { .iov_base = &req, .iov_len = sizeof(req) };
    	struct msghdr msg = {
        	.msg_name = &k_snl,
        	.msg_namelen = sizeof(k_snl),
        	.msg_iov = &iov,
        	.msg_iovlen = 1
    	};

	if(sendmsg(nds, &msg, 0) == -1)
	{
		close(nds);
		close(s);
		return (1 << 1);
	}
	
	char buf[8192] = { 0 };
	iov.iov_base = buf;
	iov.iov_len = 8192;

	__u32 groups = 1 << 1;
	ssize_t rec = recvmsg(nds, &msg, 0);
	for (struct nlmsghdr* nlh = (struct nlmsghdr*)buf; NLMSG_OK(nlh, rec); nlh = NLMSG_NEXT(nlh, rec))
	{
		if(nlh->nlmsg_type == NLMSG_DONE)
		{
			break;
		}
		else if((nlh->nlmsg_type == NLMSG_ERROR) || (nlh->nlmsg_type != SOCK_DIAG_BY_FAMILY))
		{
			close(nds);
			close(s);
			return (1 << 1);
		}
		struct netlink_diag_msg* nmsg = (struct netlink_diag_msg*)NLMSG_DATA(nlh);
		unsigned int rta_len = nlh->nlmsg_len - NLMSG_LENGTH(sizeof(*nmsg));
		if((nmsg->ndiag_family != AF_NETLINK) || (nmsg->ndiag_ino != s_st.st_ino))
		{
			close(nds);
			close(s);
			return (1 << 1);
		}
		for(struct rtattr *attr = (struct rtattr*)(nmsg + 1); RTA_OK(attr, rta_len); attr = RTA_NEXT(attr, rta_len))
		{
			if(attr->rta_type == NETLINK_DIAG_GROUPS)
			{
				groups = (*(__u32*)RTA_DATA(attr)) & ~((__u32)0x1);
				break;
			}
		}	
	}
	
	close(nds);
	close(s);
	return groups;
}

static int notify_bat(char cr)
{
	sd_bus* bus = NULL;
       	if(sd_bus_open_user(&bus) < 0)
	{
		return EC(__LINE__);
	}

	sd_bus_message* msg = NULL;
	if(sd_bus_message_new_method_call(bus, &msg, "org.freedesktop.Notifications", 
				"/org/freedesktop/Notifications", "org.freedesktop.Notifications", "Notify") < 0)
	{
		sd_bus_close_unref(bus);
		return EC(__LINE__);
	}
	
	if(sd_bus_message_append(msg, "susss", "", 0, "", (cr ? "Critical battery level." : "Low battery level."), "") < 0)
	{
		sd_bus_message_unref(msg);	
		sd_bus_close_unref(bus);
		return EC(__LINE__);
	}
	
	sd_bus_message_open_container(msg, 'a', "s");
	sd_bus_message_close_container(msg);
	
	sd_bus_message_open_container(msg, 'a', "{sv}");
	sd_bus_message_open_container(msg, 'e', "sv");
	if(sd_bus_message_append(msg, "s", "urgency") < 0)
	{
		sd_bus_message_unref(msg);	
		sd_bus_close_unref(bus);
		return EC(__LINE__);
	}
	sd_bus_message_open_container(msg, 'v', "y");
	if(sd_bus_message_append(msg, "y", (uint8_t)2) < 0)
	{
		sd_bus_message_unref(msg);	
		sd_bus_close_unref(bus);
		return EC(__LINE__);
	}
	sd_bus_message_close_container(msg);
	sd_bus_message_close_container(msg);
	sd_bus_message_close_container(msg);

	if(sd_bus_message_append(msg, "i", NTF_MS) < 0)
	{
		sd_bus_message_unref(msg);	
		sd_bus_close_unref(bus);
		return EC(__LINE__);
	}
	
	if(sd_bus_call(bus, msg, 1000000, NULL, NULL) < 0)
	{
		sd_bus_message_unref(msg);	
		sd_bus_close_unref(bus);
		return EC(__LINE__);
	}
	
	sd_bus_message_unref(msg);	
	sd_bus_close_unref(bus);
	return 0;
}

int main(void)
{
	prctl(PR_SET_NO_NEW_PRIVS, 1L, 0L, 0L, 0L);
	clearenv();
	
	int dnfd = open("/dev/null", O_RDWR);
	if(dnfd != -1)
	{
		dup2(dnfd, STDIN_FILENO);
		dup2(dnfd, STDOUT_FILENO);
		dup2(dnfd, STDERR_FILENO);
		close(dnfd);
	}
	else
	{
		close(STDIN_FILENO);
		close(STDOUT_FILENO);
		close(STDERR_FILENO);
	}
	
	uid_t cuid = getuid();
	if(!cuid)
	{
		return EC(__LINE__);
	}

	char db_addr[64] = { 0 };
	snprintf(db_addr, 64, "unix:path=/run/user/%" PRIuMAX "/bus", (uintmax_t)cuid);
	if(setenv("DBUS_SESSION_BUS_ADDRESS", db_addr, 1) == -1)
	{
		return EC(__LINE__);
	}
	
	int inf = inotify_init1(IN_CLOEXEC);
	if(inf == -1)
	{
		return EC(__LINE__);
	}
	
	snprintf(db_addr, 64, "/run/user/%" PRIuMAX, (uintmax_t)cuid);	
	int inf_w = inotify_add_watch(inf, db_addr, IN_CREATE);
	if(inf_w == -1)
	{
		close(inf);
		return EC(__LINE__);
	}
	
	char iev[sizeof(struct inotify_event) + 64 + 1] = { 0 };
	struct inotify_event* iev_p = (struct inotify_event*)iev;
	char db_addr_file[64] = { 0 };
	snprintf(db_addr_file, 64, "/run/user/%" PRIuMAX "/bus", (uintmax_t)cuid);
	if(access(db_addr_file, F_OK) == -1)
	{
		while(read(inf, &iev, sizeof(struct inotify_event) + 64 + 1) != -1)
		{
			if((iev_p->wd == inf_w) && (iev_p->mask & IN_CREATE) && strstr(iev_p->name, "bus"))
			{
				break;
			}
			memset(iev, 0, sizeof(struct inotify_event) + 64 + 1);
		}	
	}
	close(inf);

	char low = 0, crit = 0;
	int bls = open("/sys/class" BAT "/capacity", O_RDONLY | O_CLOEXEC, 0);
	if(bls == -1)
	{
		return EC(__LINE__);
	}
	else
	{
		char bls_cnt[4] = { 0 };
		if(read(bls, bls_cnt, 3) > 0)
		{
			char* end = 0;
			errno = 0;
			int lvl = (int)strtol(bls_cnt, &end, 10);
			if((lvl > -1) && (lvl < 101) && (end != bls_cnt) && !errno)
			{
				if(lvl < 16)
				{
					*(lvl < 6 ? &crit : &low) = !notify_bat((lvl < 6 ? 1 : 0));
					
				}
			}
		}
		close(bls);
	}

	struct sigaction act = { .sa_handler = sig_hand };
	sigemptyset(&act.sa_mask);
	if((sigaction(SIGABRT, &act, NULL) == -1) || (sigaction(SIGALRM, &act, NULL) == -1) || 
			(sigaction(SIGHUP, &act, NULL) == -1) || (sigaction(SIGINT, &act, NULL) == -1) || 
			(sigaction(SIGQUIT, &act, NULL) == -1) || (sigaction(SIGTERM, &act, NULL) == -1) || 
			(sigaction(SIGTSTP, &act, NULL) == -1))
	{
		return EC(__LINE__);
	}

	sigset_t smask, omask;
	sigemptyset(&smask);
	sigemptyset(&omask);

	sigaddset(&smask, SIGABRT);
	sigaddset(&smask, SIGALRM);
	sigaddset(&smask, SIGHUP);
	sigaddset(&smask, SIGINT);
	sigaddset(&smask, SIGQUIT);
	sigaddset(&smask, SIGTERM);
	sigaddset(&smask, SIGTSTP);
	
	if(sigprocmask(SIG_BLOCK, &smask, &omask) == -1)
	{
		return EC(__LINE__);
	}
	
	struct sockaddr_nl ns_snl = {
		.nl_family = AF_NETLINK,
		.nl_groups = uncover_magic_mgroups()
	};

	int ns = socket(AF_NETLINK, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, NETLINK_KOBJECT_UEVENT);
	if(ns == -1)
	{
		return EC(__LINE__);
	}

	if(bind(ns, (struct sockaddr*)&ns_snl, sizeof(ns_snl)) == -1)
	{
		close(ns);
		return EC(__LINE__);
	}
		
	int optval = 1;
	if(setsockopt(ns, SOL_NETLINK, NETLINK_NO_ENOBUFS, &optval, sizeof(optval)) == -1)
	{
		close(ns);
		return EC(__LINE__);

	}

	int es = epoll_create1(EPOLL_CLOEXEC);
	if(es == -1)
	{
		close(ns);
		return EC(__LINE__);
	}

	struct epoll_event s_ev = { .events = EPOLLIN | EPOLLET, .data.fd = ns },
			   r_ev = { 0 }; 
	if(epoll_ctl(es, EPOLL_CTL_ADD, ns, &s_ev) == -1)
	{
		close(ns);
		close(es);
		return EC(__LINE__);
	}
	
	char buf[8192] = { 0 };
	struct iovec iov = { .iov_base = buf, .iov_len = 8192 };
	struct sockaddr_nl k_snl = { .nl_family = AF_NETLINK };
	struct msghdr msg = {
		.msg_name = &k_snl,
		.msg_namelen = sizeof(k_snl),
		.msg_iov = &iov,
		.msg_iovlen = 1
	};
		
	char ac = 0, sb = 0, sa = 0, trunc = 0;
	
	int ac_fd = open("/sys/class" AC "/online", O_CLOEXEC | O_RDONLY, 0);
	if(ac_fd != -1)
	{
		char cbr = 0;
		if(read(ac_fd, &cbr, 1) == 1)
		{
			ac = cbr - '0';	
			low = ac ? 0 : low;
			crit = ac ? 0 : crit;
		}
		close(ac_fd);
	}
	
	ssize_t rs = 0;
	while(epoll_pwait(es, &r_ev, 1, -1, &omask) != -1)
	{
		while((rs = recvmsg(ns, &msg, 0)) != -1)
		{
			if(msg.msg_flags & MSG_TRUNC)
			{
				trunc = 1;
			}
			char* bcp = buf;
			while(bcp < buf + rs)
			{
				if(strstr(bcp, "DEVPATH="))
				{
					if(strstr(bcp, AC))
					{
						sa = 1;
						break;
					}
					else if(strstr(bcp, BAT))
					{
						sb = 1;
						break;
					}
				}
				bcp += strlen(bcp) + 1;
			}
			bcp = buf;
			
			if(sa)
			{
				while(bcp < buf + rs)
				{
					if(!strncmp(bcp, "POWER_SUPPLY_ONLINE=", sizeof("POWER_SUPPLY_ONLINE=") - 1))
					{
						ac = *(bcp + sizeof("POWER_SUPPLY_ONLINE=") - 1) - '0';
						low = ac ? 0 : low;
						crit = ac ? 0 : crit;
					}
					bcp += strlen(bcp) + 1;
				}
			}
			else if(sb && (!crit || !low) && !ac)
			{
				while(bcp < buf + rs)
				{	
					if(!strncmp(bcp, "POWER_SUPPLY_CAPACITY_LEVEL=", sizeof("POWER_SUPPLY_CAPACITY_LEVEL=") - 1))
					{
						if(!strncmp(bcp + sizeof("POWER_SUPPLY_CAPACITY_LEVEL"), "Critical", sizeof("Critical") - 1) && !crit)	
						{
							crit = !notify_bat(1);
						}
						else if(!strncmp(bcp + sizeof("POWER_SUPPLY_CAPACITY_LEVEL"), "Low", sizeof("Low") - 1) && !low)
						{
							low = !notify_bat(0);
						}
					}
					bcp += strlen(bcp) + 1;
				}
			}
			memset(buf, 0, (trunc ? 8192 : rs));
			trunc = sb = sa = 0;
		}
	}

	close(ns);
	close(es);
	return 0;
}

