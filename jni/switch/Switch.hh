/* A little and dumb OpenFlow 1.0 switch implemented only for demonstration purposes.
The switch is made to work with a */

#define __STDC_FORMAT_MACROS

#include <net/ethernet.h>
#include <signal.h>
#include <pthread.h>
#include <sys/socket.h>
#include "flow.hh"

#define TABLE_NUM 32

using namespace fluid_base;
using namespace fluid_msg;

uint64_t generate_dp_id() {
    srand(time(NULL));
    return (((uint64_t) rand() << 0) & 0x000000000000FFFFull) ^
           (((uint64_t) rand() << 16) & 0x00000000FFFF0000ull) ^
           (((uint64_t) rand() << 32) & 0x0000FFFF00000000ull) ^
           (((uint64_t) rand() << 48) & 0xFFFF000000000000ull);
}


class Switch : public BaseOFHandler, public OFHandler {
private:
    int id;
    bool blocking;
    EventLoop* evloop;
    pthread_t t;
    pthread_t conn_t;

    std::string address;
    int port;

    OFConnection* conn;
    uint64_t datapath_id;

    OFServerSettings ofsc;

public:
    virtual ~Switch()  {
    	if (conn != NULL)
    		delete conn;

    	delete this->evloop;
    }

    Switch(const int id = 0, std::string address = "127.0.0.1",
           const int port = 6653, uint64_t dp_id = generate_dp_id()) {
        signal(SIGPIPE, SIG_IGN);

        this->id = id;
        this->address = address;
        this->port = port;
        this->evloop = new EventLoop(0);

        this->ofsc = OFServerSettings().supported_version(0x01);
        this->datapath_id = datapath_id;
        this->conn = NULL;
    }

    static void *try_connect(void *arg) {
    	int sock;
    	struct sockaddr_in echoserver;
    	int received = 0;

    	Switch *boc = (Switch*) arg;

    	/* Create the TCP socket */
    	if ((sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    		fprintf(stderr, "Error creating socket");
    		return NULL;
    	}
    	memset(&echoserver, 0, sizeof(echoserver));
    	echoserver.sin_family = AF_INET;
    	echoserver.sin_addr.s_addr = inet_addr(boc->address.c_str());
    	echoserver.sin_port = htons(boc->port);
    	while (connect(sock, (struct sockaddr *) &echoserver, sizeof(echoserver)) < 0) {
    		fprintf(stderr, "Retrying in 5 seconds...\n");
    		sleep(5);
    	}
    	BaseOFConnection* c = new BaseOFConnection(0,
    											   boc,
    											   boc->evloop,
    											   sock,
    											   false);
    	return NULL;
    }

    void start_conn() {
        pthread_create(&conn_t, NULL,
                          &Switch::try_connect,
                           this);
    }


    bool start(bool block = false) {
        this->blocking = block;
        start_conn();
        if (not this->blocking) {
            pthread_create(&t,
                           NULL,
                           EventLoop::thread_adapter,
                           evloop);
        }
        else {
            evloop->run();
        }
        return true;
    }

    void stop_conn(){
		if (conn != NULL)
			conn->close();
	}

    void stop() {
    	stop_conn();

        //pthread_cancel(this->conn_t);
        evloop->stop();
        if (not this->blocking) {
            pthread_join(t, NULL);
        }
    }

    void connection_callback(OFConnection *conn, OFConnection::Event event_type) {
        if (event_type == OFConnection::EVENT_CLOSED) {
            stop_conn();
            start_conn();
        }
    }

    void message_callback(OFConnection *conn, uint8_t type, void *data, size_t len) {
        //Message handlers
//        if (type == of10::OFPT_BARRIER_REQUEST) {
//            this->dp->handle_barrier_request((uint8_t *) data);
//        }
//        if (type == of10::OFPT_PACKET_OUT) {
//            this->dp->handle_packet_out((uint8_t *) data);
//        }
//        if (type == of10::OFPT_FLOW_MOD) {
//            this->dp->handle_flow_mod((uint8_t *) data);
//        }
    }

    void base_message_callback(BaseOFConnection* c, void* data, size_t len) {
        uint8_t type = ((uint8_t*) data)[1];
        OFConnection* cc = (OFConnection*) c->get_manager();

        // We trust that the other end is using the negotiated protocol
        // version. Should we?

        if (ofsc.liveness_check() and type == of10::OFPT_ECHO_REQUEST) {
            uint8_t msg[8];
            memset((void*) msg, 0, 8);
            msg[0] = ((uint8_t*) data)[0];
            msg[1] = of10::OFPT_ECHO_REPLY;
            ((uint16_t*) msg)[1] = htons(8);
            ((uint32_t*) msg)[1] = ((uint32_t*) data)[1];
            // TODO: copy echo data
            c->send(msg, 8);

            if (ofsc.dispatch_all_messages()) goto dispatch; else goto done;
        }

        if (ofsc.handshake() and type == of10::OFPT_HELLO) {
            uint8_t version = ((uint8_t*) data)[0];
            if (not this->ofsc.supported_versions() & (1 << (version - 1))) {
                uint8_t msg[12];
                memset((void*) msg, 0, 8);
                msg[0] = version;
                msg[1] = of10::OFPT_ERROR;
                ((uint16_t*) msg)[1] = htons(12);
                ((uint32_t*) msg)[1] = ((uint32_t*) data)[1];
                ((uint16_t*) msg)[4] = htons(of10::OFPET_HELLO_FAILED);
                ((uint16_t*) msg)[5] = htons(of10::OFPHFC_INCOMPATIBLE);
                cc->send(msg, 12);
                cc->close();
                cc->set_state(OFConnection::STATE_FAILED);
                connection_callback(cc, OFConnection::EVENT_FAILED_NEGOTIATION);
            }

            if (ofsc.dispatch_all_messages()) goto dispatch; else goto done;
        }

        if (ofsc.liveness_check() and type == of10::OFPT_ECHO_REPLY) {
            if (ntohl(((uint32_t*) data)[1]) == ECHO_XID) {
                cc->set_alive(true);
            }

            if (ofsc.dispatch_all_messages()) goto dispatch; else goto done;
        }

        if (ofsc.handshake() and type == of10::OFPT_FEATURES_REQUEST) {
            cc->set_version(((uint8_t*) data)[0]);
            cc->set_state(OFConnection::STATE_RUNNING);
            of10::FeaturesRequest freq;
            freq.unpack((uint8_t*) data);
            of10::FeaturesReply fr(freq.xid(), this->datapath_id, 1, 1, 0x0, 0x0);
            uint8_t *buffer =  fr.pack();
            c->send(buffer, fr.length());
            OFMsg::free_buffer(buffer);

            if (ofsc.liveness_check())
                c->add_timed_callback(&Switch::send_echo, ofsc.echo_interval() * 1000, cc);
            connection_callback(cc, OFConnection::EVENT_ESTABLISHED);

            goto dispatch;
        }

        goto dispatch;

        // Dispatch a message and goto done
        dispatch:
            message_callback(cc, type, data, len);
            goto done;
        // Free the message and return
        done:
            c->free_data(data);
            return;
    }

    void base_connection_callback(BaseOFConnection* c, BaseOFConnection::Event event_type) {
        /* If the connection was closed, destroy it.
        There's no need to notify the user, since a DOWN event already
        means a CLOSED event will happen and nothing should be expected from
        the connection. */
        if (event_type == BaseOFConnection::EVENT_CLOSED) {
        	delete conn;
            return;
        }

        int conn_id = c->get_id();
        if (event_type == BaseOFConnection::EVENT_UP) {
            if (ofsc.handshake()) {
                struct of10::ofp_hello msg;
                msg.header.version = this->ofsc.max_supported_version();
                msg.header.type = of10::OFPT_HELLO;
                msg.header.length = htons(8);
                msg.header.xid = HELLO_XID;
                c->send(&msg, 8);
            }

    		this->conn = new OFConnection(c, this);
            connection_callback(this->conn, OFConnection::EVENT_STARTED);
        }
        else if (event_type == BaseOFConnection::EVENT_DOWN) {
            connection_callback(this->conn, OFConnection::EVENT_CLOSED);
        }
    }

    void free_data(void* data) {
    	BaseOFConnection::free_data(data);
    }

    static void *send_echo(void* arg) {
        OFConnection* cc = static_cast<OFConnection*>(arg);

        if (!cc->is_alive()) {
            cc->close();
            cc->get_ofhandler()->connection_callback(cc, OFConnection::EVENT_DEAD);
            return NULL;
        }

        uint8_t msg[8];
        memset((void*) msg, 0, 8);
        msg[0] = (uint8_t) cc->get_version();
        msg[1] = of10::OFPT_ECHO_REQUEST;
        ((uint16_t*) msg)[1] = htons(8);
        ((uint32_t*) msg)[1] = htonl(ECHO_XID);

        cc->set_alive(false);
        cc->send(msg, 8);

        return NULL;
    }
};
