#include "lwip/opt.h"
#include "lwip/api.h"
#include "lwip/sys.h"
#include "tcpserver.h"
#include "string.h"
#include "message.pb.h"  // Change this path based on where your generated Nanopb header is
#include <pb_decode.h>    // Include Nanopb decode header

static struct netconn *conn, *newconn;
static struct netbuf *buf;
static ip_addr_t *addr;
static unsigned short port;
char msg[100];
char smsg[200];

/**** Send RESPONSE every time the client sends some data ******/
static void tcp_thread(void *arg)
{
    err_t err, accept_err;
    struct netbuf *buf;
    struct netconn *conn, *newconn;
    char recv_data[1024]; // Buffer for receiving data
    uint16_t recv_len;    // Length of received data

    conn = netconn_new(NETCONN_TCP);

    if (conn != NULL)
    {
        err = netconn_bind(conn, IP_ADDR_ANY, 7); // Port 7
        if (err == ERR_OK)
        {
            netconn_listen(conn);
            while (1)
            {
                accept_err = netconn_accept(conn, &newconn);
                if (accept_err == ERR_OK)
                {
                    while (netconn_recv(newconn, &buf) == ERR_OK)
                    {
                        do
                        {
                            // Extract the data
                            recv_len = buf->p->len;
                            if (recv_len > sizeof(recv_data))
                                recv_len = sizeof(recv_data); // Avoid overflow

                            memcpy(recv_data, buf->p->payload, recv_len);

                            // Deserialize protobuf message using Nanopb
                            ServoUltrasonicData message = ServoUltrasonicData_init_zero;
                            pb_istream_t stream = pb_istream_from_buffer((uint8_t*)recv_data, recv_len);

                            if (pb_decode(&stream, ServoUltrasonicData_fields, &message))
                            {
                                // Prepare response string
                                char response[200];
                                int len = snprintf(response, sizeof(response),
                                    "Received servo data: Position: %f degrees, Timestamp: %f, Motor ID: %f\n",
                                    message.position_1, message.position_2, message.distance);

                                // Send response back to the client
                                netconn_write(newconn, response, len, NETCONN_COPY);
                            }
                            else
                            {
                                LWIP_DEBUGF(LWIP_DBG_ON, ("Failed to unpack message\n"));
                                const char *error_response = "Failed to unpack message";
                                netconn_write(newconn, error_response, strlen(error_response), NETCONN_COPY);
                            }

                        } while (netbuf_next(buf) > 0);

                        netbuf_delete(buf);
                    }
                    netconn_close(newconn);
                    netconn_delete(newconn);
                    vTaskDelay(pdMS_TO_TICKS(100));  // 10 ms delay
                }
            }
        }
        else
        {
            netconn_delete(conn);
        }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
}

void tcpserver_init(void)
{
	size_t freeHeapSize = xPortGetFreeHeapSize();
  sys_thread_t thread = sys_thread_new("tcp_thread", tcp_thread, NULL, DEFAULT_THREAD_STACKSIZE, osPriorityHigh);
  if (thread == NULL) {
      // Handle the error: Thread creation failed
      Error_Handler();
  }
}

