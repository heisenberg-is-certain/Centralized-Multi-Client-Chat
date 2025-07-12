This project is a command-line based multi-client chat application written entirely in C. It demonstrates core concepts of network programming using the POSIX Sockets API in a Linux/Unix environment. The application allows multiple clients to connect to a central server and broadcast messages to each other in real-time.

Two versions of the application are provided:

Manual Connection: The client requires the server's IP address to connect.

Automatic Discovery: The client automatically finds the server on the local network using a UDP broadcast mechanism.
