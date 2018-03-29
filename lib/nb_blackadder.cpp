/*
 * Copyright (C) 2010-2011  George Parisis and Dirk Trossen
 * Copyright (C) 2015-2018  Mays AL-Naday
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 3 as published by the Free Software Foundation.
 *
 * See LICENSE and COPYING for more details.
 */

#include "nb_blackadder.hpp"

NB_Blackadder* NB_Blackadder::m_pInstance = NULL;

fd_set NB_Blackadder::read_set;
fd_set NB_Blackadder::write_set;
int NB_Blackadder::pipe_fds[2] = {-1, -1};
int NB_Blackadder::sock_fd = -1;

queue <struct msghdr> NB_Blackadder::output_queue;
pthread_t NB_Blackadder::selector_thread;
pthread_mutex_t NB_Blackadder::selector_mutex;
pthread_cond_t NB_Blackadder::queue_overflow_cond;

queue <Event *> NB_Blackadder::event_queue;
pthread_t NB_Blackadder::worker_thread;
pthread_mutex_t NB_Blackadder::worker_mutex;
pthread_cond_t NB_Blackadder::worker_cond;

char NB_Blackadder::pipe_buf[1];
char NB_Blackadder::fake_buf[1];

#if HAVE_USE_NETLINK
struct sockaddr_nl NB_Blackadder::s_nladdr, NB_Blackadder::d_nladdr;
#elif HAVE_USE_UNIX
struct sockaddr_un NB_Blackadder::s_nladdr, NB_Blackadder::d_nladdr;
#endif


callbacktype NB_Blackadder::cf = NULL;

/**@relates NB_Blackadder
 * @brief This is the default Callback that will be whenever an event is received if the application hasn't registered its own callback.
 * 
 * @param ev A pointer to an already constructed Event
 */
void defaultCallback(Event *ev) {
    delete ev;
    cout << "NB_Blackadder Library: ATTENTION - user did not specify a callback function for handling events...I am falling back to default and just deleting the Event" << endl;
}

void NB_Blackadder::signal_handler(int sig) {
    (void) signal(SIGINT, SIG_DFL);
    pthread_cancel(selector_thread);
    pthread_cancel(worker_thread);
}

void *NB_Blackadder::worker(void *arg) {
    Event *ev;
    while (true) {
        pthread_mutex_lock(&worker_mutex);
        pthread_cond_wait(&worker_cond, &worker_mutex);
        while (event_queue.size() > 0) {
            ev = event_queue.front();
            event_queue.pop();
            pthread_mutex_unlock(&worker_mutex);
            cf(ev);
            pthread_mutex_lock(&worker_mutex);
        }
        pthread_mutex_unlock(&worker_mutex);
    }

    return NULL; /* Not reached unless the while-loop is terminated. */
}

void *NB_Blackadder::selector(void *arg) {
    struct msghdr msg;
    struct iovec iov;
    int total_buf_size;
    int bytes_read;
    unsigned char id_len;
    unsigned char isubID_len;
    int ret;
    int high_sock;
    unsigned char *ptr = NULL;
    if (pipe_fds[0] > sock_fd) {
        high_sock = pipe_fds[0];
    } else {
        high_sock = sock_fd;
    }
    while (true) {
        if (select(high_sock + 1, &read_set, &write_set, NULL, NULL) == -1) {
            perror("NB_Blackadder Library: select() error..retrying!");
        } else {
            if (FD_ISSET(pipe_fds[0], &read_set)) {
                /*that's a control internal message sent in the pipe*/
                ret = read(pipe_fds[0], pipe_buf, 1);
                FD_ZERO(&write_set);
                FD_SET(sock_fd, &write_set);
            }
            if (FD_ISSET(sock_fd, &read_set)) {
                /*the netlink socket is readable*/
                memset(&msg, 0, sizeof (msg));
                msg.msg_iov = &iov;
                msg.msg_iovlen = 1;
                iov.iov_base = fake_buf;
                iov.iov_len = 1;
#ifdef __linux__
                total_buf_size = recvmsg(sock_fd, &msg, MSG_PEEK | MSG_TRUNC);
#else
# ifdef __APPLE__
                socklen_t _option_len = sizeof(total_buf_size);
                if (recvmsg(sock_fd, &msg, MSG_PEEK) < 0 || getsockopt(sock_fd,
                                                                       SOL_SOCKET, SO_NREAD, &total_buf_size, &_option_len) < 0)
# else
                if (recvmsg(sock_fd, &msg, MSG_PEEK) < 0 ||
                    ioctl(sock_fd,FIONREAD, &total_buf_size) < 0)
# endif
                {
                    cout << "recvmsg/ioctl: " << errno << endl;
                    total_buf_size = -1;
                }
#endif
                
                if (total_buf_size > 0) {
                    iov.iov_base = malloc(total_buf_size);
                    iov.iov_len = total_buf_size;
                    bytes_read = recvmsg(sock_fd, &msg, 0);
                    Event *ev = new Event();
                    ev->buffer = (char *) iov.iov_base;
                    ptr = (unsigned char *)ev->buffer + sizeof(struct nlmsghdr);
                    ev->type = *ptr; ptr += sizeof(ev->type);
					id_len = *ptr; ptr += sizeof(id_len);
					ev->id = string((char *)ptr, ((int) id_len) * PURSUIT_ID_LEN);
					ptr += ((int) id_len) * PURSUIT_ID_LEN;
					if (ev->type == PUBLISHED_DATA)
						{
						ev->data = (void *)ptr;
						ev->data_len = bytes_read -
						(ptr - (unsigned char *)ev->buffer);
						}
					else if (ev->type == PUBLISHED_DATA_iSUB)
						{
						ev->nodeId = string((char *) ptr, NODEID_LEN);
						ptr += NODEID_LEN;
						isubID_len = *ptr;
						ptr += sizeof(isubID_len);
						ev->isubID = string((char *)ptr, ((int) isubID_len) * PURSUIT_ID_LEN);
						ptr += ((int) isubID_len) * PURSUIT_ID_LEN;
						ev->data = (void *)ptr;
						ev->data_len = bytes_read -
						(ptr - (unsigned char *)ev->buffer);
						}
					else
						{
						ev->data = NULL;
						ev->data_len = 0;
						}
					pthread_mutex_lock(&worker_mutex);
					event_queue.push(ev);
					pthread_cond_signal(&worker_cond);
					pthread_mutex_unlock(&worker_mutex);

                } else {
                    //perror("NB_Blackadder Library: did not read ");
                    /*DO NOT call the callback function*/
                }
            }
            if (FD_ISSET(sock_fd, &write_set)) {
                /*the netlink socket is writable*/
                pthread_mutex_lock(&selector_mutex);
                msg = output_queue.front();
                ret = sendmsg(sock_fd, &msg, MSG_WAITALL);
                if (ret > 0) {
                    output_queue.pop();
                    if (msg.msg_iovlen == 2) {
                        free(msg.msg_iov[0].iov_base);
                        msg.msg_iov[0].iov_base = NULL;
                        free(msg.msg_iov[1].iov_base);
                        msg.msg_iov[1].iov_base = NULL;
                    } else {
                        free(msg.msg_iov->iov_base);
                        msg.msg_iov->iov_base = NULL;
                    }
                    free(msg.msg_iov);
                    msg.msg_iov = NULL;
                }
                //                else {
                //                    perror("NB_Blackadder Library: could not write!!");
                //                }
                FD_ZERO(&write_set);
                if (output_queue.size() > 0) {
                    FD_ZERO(&write_set);
                    FD_SET(sock_fd, &write_set);
                } else {
                    FD_ZERO(&write_set);
                    pthread_cond_signal(&queue_overflow_cond);
                }
                pthread_mutex_unlock(&selector_mutex);
            }
            FD_ZERO(&read_set);
            FD_SET(sock_fd, &read_set);
            FD_SET(pipe_fds[0], &read_set);
        }
    }
    
    return NULL; /* Not reached unless the while-loop is terminated. */
}

NB_Blackadder::NB_Blackadder(bool user_space) {
    int ret;
    (void) signal(SIGINT, signal_handler);
    if (user_space) {
        //cout << "NB_Blackadder Library: Initializing blackadder client for user space" << endl;
#if HAVE_USE_NETLINK
        sock_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC);
#elif HAVE_USE_UNIX
        sock_fd = socket(PF_LOCAL, SOCK_DGRAM, 0);
#endif
    } else {
        //cout << "NB_Blackadder Library: Initializing blackadder client for kernel space" << endl;
#if HAVE_USE_NETLINK
        sock_fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_BADDER);
#elif __FreeBSD__ /* XXX */
        sock_fd = socket(AF_BLACKADDER, SOCK_RAW, PROTO_BLACKADDER);
#else
        sock_fd = -1;
        errno = EPFNOSUPPORT; /* XXX */
#endif
    }
    if (sock_fd < 0) {
        perror("NB_Blackadder Library: socket");
    } else {
        //cout << "NB_Blackadder Library: Created and opened netlink socket" << endl;
    }
    int x;
    x = fcntl(sock_fd, F_GETFL, 0);
    fcntl(sock_fd, F_SETFL, x | O_NONBLOCK);
#ifdef __APPLE__
    int bufsize = 229376; /* XXX */
    setsockopt(sock_fd, SOL_SOCKET, SO_SNDBUF, &bufsize, sizeof(bufsize));
    setsockopt(sock_fd, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(bufsize));
#endif
    /* source address */
    memset(&s_nladdr, 0, sizeof (s_nladdr));
#if HAVE_USE_NETLINK
    s_nladdr.nl_family = AF_NETLINK;
    s_nladdr.nl_pad = 0;
    s_nladdr.nl_pid = getpid();
    ret = bind(sock_fd, (struct sockaddr *) &s_nladdr, sizeof (s_nladdr));
#elif HAVE_USE_UNIX
    if (user_space) {
# ifndef __linux__
        s_nladdr.sun_len = sizeof (s_nladdr);
# endif
        s_nladdr.sun_family = PF_LOCAL;
        /* XXX: Probably shouldn't use getpid() here. */
        ba_id2path(s_nladdr.sun_path, getpid());
        if (unlink(s_nladdr.sun_path) != 0 && errno != ENOENT)
            perror("unlink");
# ifdef __linux__
        ret = bind(sock_fd, (struct sockaddr *) &s_nladdr, sizeof(s_nladdr));
# else
        ret = bind(sock_fd, (struct sockaddr *) &s_nladdr, SUN_LEN(&s_nladdr));
# endif
    } else {
        if (sock_fd > 0)
            ret = 0;
        else {
            ret = -1;
            errno = EBADF;
        }
    }
#endif
    if (ret < 0) {
        perror("NB_Blackadder Library: bind");
    }
    /* destination address */
    memset(&d_nladdr, 0, sizeof (d_nladdr));
#if HAVE_USE_NETLINK
    d_nladdr.nl_family = AF_NETLINK;
    d_nladdr.nl_pad = 0;
    if (user_space) {
        d_nladdr.nl_pid = PID_BLACKADDER; /* destined to user space blackadder */
    } else {
        d_nladdr.nl_pid = 0; /* destined to kernel */
    }
#elif HAVE_USE_UNIX
    if (user_space) {
# ifndef __linux__
        d_nladdr.sun_len = sizeof (d_nladdr);
# endif
        d_nladdr.sun_family = PF_LOCAL;
        ba_id2path(d_nladdr.sun_path, (user_space) ? PID_BLACKADDER : 0);
    }
#endif
    /*initialize pipes*/
    if (pipe(pipe_fds) != 0) {
        perror("pipe");
        /* XXX: Should we raise an exception or something? */
    }
    FD_ZERO(&read_set);
    FD_SET(sock_fd, &read_set);
    FD_SET(pipe_fds[0], &read_set);
    /*register default callback method*/
    cf = &defaultCallback;
    pthread_mutex_init(&selector_mutex, NULL);
    pthread_mutex_init(&worker_mutex, NULL);
    pthread_cond_init(&queue_overflow_cond, NULL);
    pthread_cond_init(&worker_cond, NULL);
    pthread_create(&selector_thread, NULL, selector, NULL);
    pthread_create(&worker_thread, NULL, worker, NULL);
}

NB_Blackadder::~NB_Blackadder() {
    cout << "NB_Blackadder Library: deleting Blackadder..." << endl;
    pthread_mutex_lock(&selector_mutex);
    if (output_queue.size() != 0) {
        pthread_cond_wait(&queue_overflow_cond, &selector_mutex);
    }
    pthread_mutex_unlock(&selector_mutex);
    pthread_cancel(worker_thread);
    pthread_cancel(selector_thread);
    if (sock_fd != -1) {
        close(sock_fd);
        cout << "NB_Blackadder Library: Closed netlink socket" << endl;
#if HAVE_USE_UNIX
        unlink(s_nladdr.sun_path);
#endif
    }
    for (int i = 0; i < (sizeof(pipe_fds)/sizeof(pipe_fds[0])); ++i) {
        if (pipe_fds[i] != -1)
            close(pipe_fds[i]);
    }
}

NB_Blackadder* NB_Blackadder::Instance(bool user_space) {
    if (!m_pInstance) {
        m_pInstance = new NB_Blackadder(user_space);
    }
    return m_pInstance;
}

void NB_Blackadder::join() {
    pthread_join(selector_thread, NULL);
    pthread_join(worker_thread, NULL);
}

void NB_Blackadder::setCallback(callbacktype function) {
    cf = function;
}

void NB_Blackadder::publish_scope(const string &id, const string &prefix_id, unsigned char strategy, void *str_opt, unsigned int str_opt_len) {
    if (id.length() % PURSUIT_ID_LEN != 0) {
        cout << "NB_Blackadder Library: Could not send PUBLISH_SCOPE request - wrong ID size" << endl;
    } else if (prefix_id.length() % PURSUIT_ID_LEN != 0) {
        cout << "NB_Blackadder Library: Could not send PUBLISH_SCOPE request - wrong prefix_id size" << endl;
    } else if (id.length() == 0) {
        cout << "NB_Blackadder Library: Could not send PUBLISH_SCOPE request - id cannot be empty" << endl;
    } else {
        push(PUBLISH_SCOPE, id, prefix_id, strategy, str_opt, str_opt_len);
    }
}

void NB_Blackadder::publish_info(const string &id, const string &prefix_id, unsigned char strategy, void *str_opt, unsigned int str_opt_len) {
    if (id.length() % PURSUIT_ID_LEN != 0) {
        cout << "NB_Blackadder Library: Could not send PUBLISH_INFO request - wrong ID size" << endl;
    } else if (prefix_id.length() % PURSUIT_ID_LEN != 0) {
        cout << "NB_Blackadder Library: Could not send PUBLISH_INFO request - wrong prefix_id size" << endl;
    } else if (prefix_id.length() == 0) {
        cout << "NB_Blackadder Library: Could not send PUBLISH_INFO request - prefix_id cannot be empty" << endl;
    } else if (prefix_id.length() == 0) {
        cout << "NB_Blackadder Library: Could not send PUBLISH_INFO request - prefix_id cannot be empty" << endl;
    } else {
        push(PUBLISH_INFO, id, prefix_id, strategy, str_opt, str_opt_len);
    }
}

void NB_Blackadder::unpublish_scope(const string &id, const string &prefix_id, unsigned char strategy, void *str_opt, unsigned int str_opt_len) {
    if (id.length() % PURSUIT_ID_LEN != 0) {
        cout << "NB_Blackadder Library: Could not send UNPUBLISH_SCOPE request - wrong ID size" << endl;
    } else if (prefix_id.length() % PURSUIT_ID_LEN != 0) {
        cout << "NB_Blackadder Library: Could not send UNPUBLISH_SCOPE request - wrong prefix_id size" << endl;
    } else if (id.length() == 0) {
        cout << "NB_Blackadder Library: Could not send UNPUBLISH_SCOPE request - id cannot be empty" << endl;
    } else if (id.length() / PURSUIT_ID_LEN > 1) {
        cout << "NB_Blackadder Library: Could not send UNPUBLISH_SCOPE request - id cannot consist of multiple fragments" << endl;
    } else {
        push(UNPUBLISH_SCOPE, id, prefix_id, strategy, str_opt, str_opt_len);
    }
}

void NB_Blackadder::unpublish_info(const string &id, const string &prefix_id, unsigned char strategy, void *str_opt, unsigned int str_opt_len) {
    if (id.length() % PURSUIT_ID_LEN != 0) {
        cout << "NB_Blackadder Library: Could not send UNPUBLISH_INFO request - wrong ID size" << endl;
    } else if (prefix_id.length() % PURSUIT_ID_LEN != 0) {
        cout << "NB_Blackadder Library: Could not send UNPUBLISH_INFO request - wrong prefix_id size" << endl;
    } else if (id.length() == 0) {
        cout << "NB_Blackadder Library: Could not send UNPUBLISH_INFO request - id cannot be empty" << endl;
    } else if (prefix_id.length() == 0) {
        cout << "NB_Blackadder Library: Could not send UNPUBLISH_INFO request - prefix_id cannot be empty" << endl;
    } else if (id.length() / PURSUIT_ID_LEN > 1) {
        cout << "NB_Blackadder Library: Could not send UNPUBLISH_INFO request - id cannot consist of multiple fragments" << endl;
    } else {
        push(UNPUBLISH_INFO, id, prefix_id, strategy, str_opt, str_opt_len);
    }
}

void NB_Blackadder::subscribe_scope(const string &id, const string &prefix_id, unsigned char strategy, void *str_opt, unsigned int str_opt_len) {
    if (id.length() % PURSUIT_ID_LEN != 0) {
        cout << "NB_Blackadder Library: Could not send SUBSCRIBE_SCOPE request - wrong ID size" << endl;
    } else if (prefix_id.length() % PURSUIT_ID_LEN != 0) {
        cout << "NB_Blackadder Library: Could not send SUBSCRIBE_SCOPE request - wrong prefix_id size" << endl;
    } else if (id.length() == 0) {
        cout << "NB_Blackadder Library: Could not send SUBSCRIBE_SCOPE request - id cannot be empty" << endl;
    } else if (id.length() / PURSUIT_ID_LEN > 1) {
        cout << "NB_Blackadder Library: Could not send SUBSCRIBE_SCOPE request - id cannot consist of multiple fragments" << endl;
    } else {
        push(SUBSCRIBE_SCOPE, id, prefix_id, strategy, str_opt, str_opt_len);
    }
}

void NB_Blackadder::subscribe_info(const string &id, const string &prefix_id, unsigned char strategy, void *str_opt, unsigned int str_opt_len) {
    if (id.length() % PURSUIT_ID_LEN != 0) {
        cout << "NB_Blackadder Library: Could not send SUBSCRIBE_INFO request - wrong ID size" << endl;
    } else if (prefix_id.length() % PURSUIT_ID_LEN != 0) {
        cout << "NB_Blackadder Library: Could not send SUBSCRIBE_INFO request - wrong prefix_id size" << endl;
    } else if (id.length() == 0) {
        cout << "NB_Blackadder Library: Could not send SUBSCRIBE_INFO request - id cannot be empty" << endl;
    } else if (prefix_id.length() == 0) {
        cout << "NB_Blackadder Library: Could not send SUBSCRIBE_INFO request - prefix_id cannot be empty" << endl;
    } else if (id.length() / PURSUIT_ID_LEN > 1) {
        cout << "NB_Blackadder Library: Could not send SUBSCRIBE_INFO request - id cannot consist of multiple fragments" << endl;
    } else {
        push(SUBSCRIBE_INFO, id, prefix_id, strategy, str_opt, str_opt_len);
    }
}

void NB_Blackadder::unsubscribe_scope(const string &id, const string &prefix_id, unsigned char strategy, void *str_opt, unsigned int str_opt_len) {
    if (id.length() % PURSUIT_ID_LEN != 0) {
        cout << "NB_Blackadder Library: Could not send UNSUBSCRIBE_SCOPE request - wrong ID size" << endl;
    } else if (prefix_id.length() % PURSUIT_ID_LEN != 0) {
        cout << "NB_Blackadder Library: Could not send UNSUBSCRIBE_SCOPE request - wrong prefix_id size" << endl;
    } else if (id.length() == 0) {
        cout << "NB_Blackadder Library: Could not send UNSUBSCRIBE_SCOPE request - id cannot be empty" << endl;
    } else if (id.length() / PURSUIT_ID_LEN > 1) {
        cout << "NB_Blackadder Library: Could not send UNSUBSCRIBE_SCOPE request - id cannot consist of multiple fragments" << endl;
    } else {
        push(UNSUBSCRIBE_SCOPE, id, prefix_id, strategy, str_opt, str_opt_len);
    }
}

void NB_Blackadder::unsubscribe_info(const string &id, const string &prefix_id, unsigned char strategy, void *str_opt, unsigned int str_opt_len) {
    if (id.length() % PURSUIT_ID_LEN != 0) {
        cout << "NB_Blackadder Library: Could not send UNSUBSCRIBE_INFO request - wrong ID size" << endl;
    } else if (prefix_id.length() % PURSUIT_ID_LEN != 0) {
        cout << "NB_Blackadder Library: Could not send UNSUBSCRIBE_INFO request - wrong prefix_id size" << endl;
    } else if (id.length() == 0) {
        cout << "NB_Blackadder Library: Could not send UNSUBSCRIBE_INFO request - id cannot be empty" << endl;
    } else if (prefix_id.length() == 0) {
        cout << "NB_Blackadder Library: Could not send UNSUBSCRIBE_INFO request - prefix_id cannot be empty" << endl;
    } else if (id.length() / PURSUIT_ID_LEN > 1) {
        cout << "NB_Blackadder Library: Could not send UNSUBSCRIBE_INFO request - id cannot consist of multiple fragments" << endl;
    } else {
        push(UNSUBSCRIBE_INFO, id, prefix_id, strategy, str_opt, str_opt_len);
    }
}

void NB_Blackadder::push(unsigned char type, const string &id, const string &prefix_id, char strategy, void *str_opt, unsigned int str_opt_len) {
    int ret;
    char *buffer;
    int buffer_length;
    struct nlmsghdr *nlh;
    struct msghdr msg;
    struct iovec *iov = (struct iovec *) calloc(1, sizeof (struct iovec));
    unsigned char id_len = id.length() / PURSUIT_ID_LEN;
    unsigned char prefix_id_len = prefix_id.length() / PURSUIT_ID_LEN;

    if (str_opt == NULL) {
        buffer_length = sizeof (struct nlmsghdr) + sizeof (type) + sizeof (id_len) + id.length() + sizeof (prefix_id_len) + prefix_id.length() + sizeof (strategy);
    } else {
        buffer_length = sizeof (struct nlmsghdr) + sizeof (type) + sizeof (id_len) + id.length() + sizeof (prefix_id_len) + prefix_id.length() + sizeof (strategy) + str_opt_len;
    }
    buffer = (char *) malloc(buffer_length);
    nlh = (struct nlmsghdr *) buffer;
    nlh->nlmsg_len = buffer_length;
    nlh->nlmsg_pid = getpid();
    nlh->nlmsg_flags = 1;
    nlh->nlmsg_type = 0;
    memcpy(buffer + sizeof (struct nlmsghdr), &type, sizeof (type));
    memcpy(buffer + sizeof (struct nlmsghdr) + sizeof (type), &id_len, sizeof (id_len));
    memcpy(buffer + sizeof (struct nlmsghdr) + sizeof (type) + sizeof (id_len), id.c_str(), id.length());
    memcpy(buffer + sizeof (struct nlmsghdr) + sizeof (type) + sizeof (id_len) + id.length(), &prefix_id_len, sizeof (prefix_id_len));
    memcpy(buffer + sizeof (struct nlmsghdr) + sizeof (type) + sizeof (id_len) + id.length() + sizeof (prefix_id_len), prefix_id.c_str(), prefix_id.length());
    memcpy(buffer + sizeof (struct nlmsghdr) + sizeof (type) + sizeof (id_len) + id.length() + sizeof (prefix_id_len) + prefix_id.length(), &strategy, sizeof (strategy));
    if (str_opt != NULL) {
        memcpy(buffer + sizeof (struct nlmsghdr) + sizeof (type) + sizeof (id_len) + id.length() + sizeof (prefix_id_len) + prefix_id.length() + sizeof (strategy), str_opt, str_opt_len);
    }
    memset(&msg, 0, sizeof (msg));
    iov->iov_base = buffer;
    iov->iov_len = buffer_length;
    msg.msg_name = (void *) &d_nladdr;
    msg.msg_namelen = sizeof (d_nladdr);
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    pthread_mutex_lock(&selector_mutex);
    output_queue.push(msg);
    if (output_queue.size() == 1) {
        ret = write(pipe_fds[1], pipe_buf, 1);
    }
    if (output_queue.size() == 1000) {
        pthread_cond_wait(&queue_overflow_cond, &selector_mutex);
    }
    pthread_mutex_unlock(&selector_mutex);
}

void NB_Blackadder::publish_data(const string &id, unsigned char strategy, void *str_opt, unsigned int str_opt_len, void *a_data, unsigned int data_len) {
    int ret;
    void *data = a_data;
    char *buffer;
    int buffer_length;
    struct nlmsghdr *nlh;
    struct msghdr msg;
    unsigned char type = PUBLISH_DATA;
    struct iovec *iov = (struct iovec *) calloc(2, sizeof (struct iovec));
    unsigned char id_len = id.length() / PURSUIT_ID_LEN;
    if (iov == NULL) {
        perror("calloc iovecs failed");
        return;
    }

    if (str_opt == NULL) {
        buffer_length = sizeof (struct nlmsghdr) + sizeof (type) + sizeof (id_len) + id.length() + sizeof (strategy);
    } else {
        buffer_length = sizeof (struct nlmsghdr) + sizeof (type) + sizeof (id_len) + id.length() + sizeof (strategy) + str_opt_len;
    }

    buffer = (char *) malloc(buffer_length);

    nlh = (struct nlmsghdr *) buffer;
    nlh->nlmsg_len = buffer_length + data_len;
    nlh->nlmsg_pid = getpid();
    nlh->nlmsg_flags = 1;
    nlh->nlmsg_type = 0;
    memcpy(buffer + sizeof (struct nlmsghdr), &type, sizeof (type));
    memcpy(buffer + sizeof (struct nlmsghdr) + sizeof (type), &id_len, sizeof (id_len));
    memcpy(buffer + sizeof (struct nlmsghdr) + sizeof (type) + sizeof (id_len), id.c_str(), id.length());
    memcpy(buffer + sizeof (struct nlmsghdr) + sizeof (type) + sizeof (id_len) + id.length(), &strategy, sizeof (strategy));
    if (str_opt != NULL) {
        memcpy(buffer + sizeof (struct nlmsghdr) + sizeof (type) + sizeof (id_len) + id.length() + sizeof (strategy), str_opt, str_opt_len);
    }
    memset(&msg, 0, sizeof (msg));
    iov[0].iov_base = buffer;
    iov[0].iov_len = buffer_length;
    iov[1].iov_base = data;
    iov[1].iov_len = data_len;
    msg.msg_name = (void *) &d_nladdr;
    msg.msg_namelen = sizeof (d_nladdr);
    msg.msg_iov = iov;
    msg.msg_iovlen = 2;

    pthread_mutex_lock(&selector_mutex);
    output_queue.push(msg);
    if (output_queue.size() == 1) {
        ret = write(pipe_fds[1], pipe_buf, 1);
    }
    if (output_queue.size() == 1000) {
        pthread_cond_wait(&queue_overflow_cond, &selector_mutex);
    }
    pthread_mutex_unlock(&selector_mutex);
}
bool NB_Blackadder::publish_data(const string &id,
                                 unsigned char strategy,
                                 void *str_opt,
                                 unsigned int str_opt_len,
                                 std::list<string> &nodeIds,
                                 void *a_data,
                                 unsigned int data_len
                                 ){
    int ret;
    void *data = a_data;
    char *buffer;
    int buffer_length;
    struct nlmsghdr *nlh;
    struct msghdr msg;
    unsigned char type = PUBLISH_DATA_iMULTICAST;
    struct iovec *iov = (struct iovec *) calloc(2, sizeof (struct iovec));
    unsigned char id_len = id.length() / PURSUIT_ID_LEN;
    unsigned char nodeIdListSize = nodeIds.size();
    string nodeIdsStream;
    for (std::list<string>::iterator it = nodeIds.begin(); it != nodeIds.end(); ++it) {
        nodeIdsStream += (*it);
    }
    if (iov == NULL)
    {
        perror("calloc iovecs failed");
        return false;
    }
    if (str_opt == NULL)
    {
        buffer_length = sizeof (struct nlmsghdr) + sizeof (type)
        + sizeof (id_len) + id.length() + sizeof (strategy)
        + sizeof(nodeIdListSize)
        + nodeIdListSize * PURSUIT_ID_LEN;
    }
    else
    {
        buffer_length = sizeof (struct nlmsghdr) + sizeof (type)
        + sizeof (id_len) + id.length() + sizeof (strategy)
        + str_opt_len + sizeof(nodeIdListSize) + nodeIdListSize * PURSUIT_ID_LEN;
    }
    buffer = (char *) malloc(buffer_length);
    nlh = (struct nlmsghdr *) buffer;
    nlh->nlmsg_len = buffer_length + data_len;
    nlh->nlmsg_pid = getpid();
    nlh->nlmsg_flags = 1;
    nlh->nlmsg_type = 0;
    memcpy(buffer + sizeof (struct nlmsghdr), &type, sizeof (type));
    memcpy(buffer + sizeof (struct nlmsghdr) + sizeof (type), &id_len,
           sizeof (id_len));
    memcpy(buffer + sizeof (struct nlmsghdr) + sizeof (type) + sizeof (id_len),
           id.c_str(), id.length());
    memcpy(buffer + sizeof (struct nlmsghdr) + sizeof (type) + sizeof (id_len)
           + id.length(), &strategy, sizeof (strategy));
    if (str_opt != NULL)
    {
        memcpy(buffer + sizeof (struct nlmsghdr) + sizeof (type)
               + sizeof (id_len) + id.length() + sizeof (strategy),
               str_opt, str_opt_len);
        // Adding nodeIdListSize
        memcpy(buffer + sizeof (struct nlmsghdr) + sizeof (type)
               + sizeof (id_len) + id.length() + sizeof (strategy)
               + str_opt_len, &nodeIdListSize, sizeof (nodeIdListSize));
        // Adding list of Node IDs
        memcpy(buffer + sizeof (struct nlmsghdr) + sizeof (type)
               + sizeof (id_len) + id.length() + sizeof (strategy)
               + str_opt_len + sizeof(nodeIdListSize), (void *) nodeIdsStream.c_str(),
               nodeIdListSize * PURSUIT_ID_LEN);
    }
    else
    {
        // Adding nodeIdListSize
        memcpy(buffer + sizeof (struct nlmsghdr) + sizeof (type)
               + sizeof (id_len) + id.length() + sizeof (strategy),
               &nodeIdListSize, sizeof (nodeIdListSize));
        // Adding list of Node IDs
        memcpy(buffer + sizeof (struct nlmsghdr) + sizeof (type)
               + sizeof (id_len) + id.length() + sizeof (strategy)
               + sizeof(nodeIdListSize), (void *) nodeIdsStream.c_str(),
               
               nodeIdListSize * PURSUIT_ID_LEN);
    }
    memset(&msg, 0, sizeof (msg));
    iov[0].iov_base = buffer;
    iov[0].iov_len = buffer_length;
    iov[1].iov_base = data;
    iov[1].iov_len = data_len;
    msg.msg_name = (void *) &d_nladdr;
    msg.msg_namelen = sizeof (d_nladdr);
    msg.msg_iov = iov;
    msg.msg_iovlen = 2;
    pthread_mutex_lock(&selector_mutex);
    output_queue.push(msg);
    if (output_queue.size() == 1) {
        ret = write(pipe_fds[1], pipe_buf, 1);
        if (ret < 0)
        {
            return false;
        }
    }
    if (output_queue.size() == 1000) {
        pthread_cond_wait(&queue_overflow_cond, &selector_mutex);
    }
    pthread_mutex_unlock(&selector_mutex);
    return true;
}

bool NB_Blackadder::publish_data_isub(const string &id,
                                      unsigned char strategy,
                                      void *str_opt,
                                      unsigned int str_opt_len,
                                      const string &isubID,
                                      void *a_data,
                                      unsigned int data_len
                                      ){
    int ret;
    void *data = a_data;
    char *buffer;
    int buffer_length;
    struct nlmsghdr *nlh;
    struct msghdr msg;
    unsigned char type = PUBLISH_DATA_iSUB;
    struct iovec *iov = (struct iovec *) calloc(2, sizeof (struct iovec));
    unsigned char id_len = id.length() / PURSUIT_ID_LEN;
    unsigned char isubID_len = isubID.length() / PURSUIT_ID_LEN;
    if (iov == NULL)
    {
        perror("calloc iovecs failed");
        return false;
    }
    if (str_opt == NULL)
    {
        buffer_length = sizeof (struct nlmsghdr) + sizeof (type)
        + sizeof (id_len) + id.length() + sizeof (strategy)
        + sizeof (isubID_len) + isubID.length();
    }
    else
    {
        buffer_length = sizeof (struct nlmsghdr) + sizeof (type)
        + sizeof (id_len) + id.length() + sizeof (strategy)
        + str_opt_len + sizeof (isubID_len) + isubID.length();
    }
    buffer = (char *) malloc(buffer_length);
    nlh = (struct nlmsghdr *) buffer;
    nlh->nlmsg_len = buffer_length + data_len;
    nlh->nlmsg_pid = getpid();
    nlh->nlmsg_flags = 1;
    nlh->nlmsg_type = 0;
    memcpy(buffer + sizeof (struct nlmsghdr), &type, sizeof (type));
    memcpy(buffer + sizeof (struct nlmsghdr) + sizeof (type), &id_len,
           sizeof (id_len));
    memcpy(buffer + sizeof (struct nlmsghdr) + sizeof (type) + sizeof (id_len),
           id.c_str(), id.length());
    memcpy(buffer + sizeof (struct nlmsghdr) + sizeof (type) + sizeof (id_len)
           + id.length(), &strategy, sizeof (strategy));
    if (str_opt != NULL)
    {
        memcpy(buffer + sizeof (struct nlmsghdr) + sizeof (type)
               + sizeof (id_len) + id.length() + sizeof (strategy),
               str_opt, str_opt_len);
        /*Adding the size of isubID in PURSUIT_ID_LEN fragments*/
        memcpy(buffer + sizeof (struct nlmsghdr) + sizeof (type)
               + sizeof (id_len) + id.length() + sizeof (strategy)
               + str_opt_len, &isubID_len, sizeof (isubID_len));
        /*Adding isubID*/
        memcpy(buffer + sizeof (struct nlmsghdr) + sizeof (type)
               + sizeof (id_len) + id.length() + sizeof (strategy)
               + str_opt_len + sizeof (isubID_len), isubID.c_str(), isubID.length());
        
    }
    else
    {
        /*Adding the size of isubID in PURSUIT_ID_LEN fragments*/
        memcpy(buffer + sizeof (struct nlmsghdr) + sizeof (type)
               + sizeof (id_len) + id.length() + sizeof (strategy),
               &isubID_len, sizeof (isubID_len));
        /*Adding isubID*/
        memcpy(buffer + sizeof (struct nlmsghdr) + sizeof (type)
               + sizeof (id_len) + id.length() + sizeof (strategy) + sizeof (isubID_len),
               isubID.c_str(), isubID.length());
        
        
    }
    memset(&msg, 0, sizeof (msg));
    iov[0].iov_base = buffer;
    iov[0].iov_len = buffer_length;
    iov[1].iov_base = data;
    iov[1].iov_len = data_len;
    msg.msg_name = (void *) &d_nladdr;
    msg.msg_namelen = sizeof (d_nladdr);
    msg.msg_iov = iov;
    msg.msg_iovlen = 2;
    pthread_mutex_lock(&selector_mutex);
    output_queue.push(msg);
    if (output_queue.size() == 1) {
        ret = write(pipe_fds[1], pipe_buf, 1);
        if (ret < 0)
        {
            return false;
        }
    }
    if (output_queue.size() == 1000) {
        pthread_cond_wait(&queue_overflow_cond, &selector_mutex);
    }
    pthread_mutex_unlock(&selector_mutex);
    return true;
}

bool NB_Blackadder::disconnect() {
    if (sock_fd == -1) {
        cout << "NB_Blackadder Library: Socket already closed" << endl;
        return false;
    }
    int ret;
    struct msghdr msg;
    struct iovec iov[2];
    struct nlmsghdr _nlh, *nlh = &_nlh;
    memset(&msg, 0, sizeof(msg));
    memset(iov, 0, sizeof(iov));
    memset(nlh, 0, sizeof(*nlh));
    unsigned char type = DISCONNECT;
    /* Fill the netlink message header */
    nlh->nlmsg_len = sizeof (struct nlmsghdr) + 1 /*type*/;
    nlh->nlmsg_pid = getpid();
    nlh->nlmsg_flags = 1;
    nlh->nlmsg_type = 0;
    iov[0].iov_base = nlh;
    iov[0].iov_len = sizeof (*nlh);
    iov[1].iov_base = &type;
    iov[1].iov_len = sizeof (type);
    memset(&msg, 0, sizeof (msg));
    msg.msg_name = (void *) &d_nladdr;
    msg.msg_namelen = sizeof (d_nladdr);
    msg.msg_iov = iov;
    msg.msg_iovlen = 2;
    ret = sendmsg(sock_fd, &msg, 0);
    close(sock_fd);
#if HAVE_USE_UNIX
    unlink(s_nladdr.sun_path);
#endif
    sock_fd = -1;
    if (ret < 0)
    {
		perror("NB_Blackadder Library: failed to send disconnection message");
		return false;
	}
    return true;
}
