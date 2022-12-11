# Client-Server-Requests
This program was a project in my 'Operating Systems' class at UofL. It
was meant to have the client and server programs run side-by-side with
the server program being called first. The project was used to teach
shared memory, basic file structure, semaphores, and file locking.

---  Server  ---
The server program would initiate a shared memory to be used as a message
queue between the client and server to pass requests along. Then the server
would create a child process to handle each request. The request would hold
an absolute file path and a keyword. The child would navigate to the file
path, create a thread for each file within that path, and create a printer
thread. Each of the threads attached to the individual files would read
through the file line by line searching for the keyword, if it was found, the
thread would add the keyword to a linked-list array. The printer thread would
pull from this linked-list array and append it to an 'output.txt' file. The
printer thread would also have to utilize file locking as the 'output.txt' would
be accessed by every other child process that had been created for the separate
requests passed through.

---  Client  ---
The client program would find and open a pointer to the shared memory address
created by the server program. Then the client would read line by line through
a specified file pulling each line as a new request, an absolute file path, and a
keyword. Each of these requests would be added to the shared memory queue.
