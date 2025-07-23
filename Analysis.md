Haifeng Zheng
Professor Ada Gavrilovska
CS6200 -Graduate
Intro to OS 9/20/2024
Read me
Part 1
This part of the project is essential an extension of the previous multi-threading project. We don’t need to worry about the multi-threading implementation of the proxy. We only care about the part where we are retrieving the file from the web instead of from the disc. We then incorporate the Getfile API to send the data to the client. Luckily, the libcurl’s “easy” C interface allows a straightforward retrieval of information from the web.
Implementation:
webproxy.c:
Not much was changed in this file. The only change was passing the link of the server to the worker thread. For my implementation of the handle_with_curl, it doesn’t require anything else other than the server link.
handle_with_curl.c:
To retrieve data from the server, I will be using libcurl’s “easy” C interface. The first thing I thought about when I was using the easy interface was how can I get the header data and send it before I send the actual data of the file.
There were multiple ways to do this, one such way was using the curl_easy_getinfo() function to get the length and the status. Then store that information into an object and then pass the object into the curl_easy_setopt, CURLOPT_WRITEDATA option.
Alternatively, I could use the CULOPT_HEADERFUNCTION option of curl_easy_setopt to parse the received header and store the length and status into an object that is then passed into CURLOPT_WRITEDATA option.
Ultimately, I chose the second option because I felt it would end up with cleaner code.
For the implementation of the latter option, I first need to create the struct that will be passed into both CURLOPT_WRITEDATA and CURLOPT_HEADERDATA options. This struct, “request_data”, consists of three variables:
•
gfcontext_t *context. This stores the pointer of the gfcontext.
•
int status. This stores 200, 404, or any other status. This is the status that the request to the server returns. 200: everything is good. 400: file not found. Default equals -1.
•
Header_sent: This will be used later in the main write_callback to tell the callback whether or not the header has been sent to the client or not. Default equals -1.
Now that the struct is created, the header_callback is pretty straightforward. The header_callback is called multiple times by the curl_easy_perform. A complete header line is then passed onto the callback each time. (CURLOPT_HEADERFUNCTION documentation) This makes it easy to parse the header. If the passed header contains “Content-Length” then we parse the integer right after that string and store it into “context->file_len” of the request_data struct. If it contains “HTTP/1.1” we store the integer right into the “status” of the request_data struct.
Another consideration was whether or not to call the gf_sendheader in here. I ultimately chose not to because I was unable to find any conclusive sources that say the entirety of the header_callbacks will be called before the write_callback is called. I want to ensure that the header is sent to the client before any data is sent. So, I decided that the gf_sendheader would be called in the write_callback. This was the reason why the header_sent variable existed inside the request_data struct. It is a way to let the write_callback know if gf_sendheader() has already been called. This will be explained later.
On to the implementation of the write_callback. The data that is passed into the write_callback of the easy curl is the same structure that is passed into the header_callback, “request_data”.
The write-callback similar to the header-callback is called multiple times by the EASY CURL until the data is completely sent. A part of the data is passed into write_callback each time the callback is called. (CURLOPT_WRITEFUNCTION DOCUMENTATION)
As mentioned earlier, the gf_sendheader will be called in here, and because the write_Callback is called multiple times by the EASY CURL, I need a way to tell the callback to only send the gf_sendheader once. I chose to use the variable “header_sent” within the “request_data” struct. If “header_sent” equals 0 that means that the header has not been sent yet. The write_callback will loop continuously, checking if status = 200, 404, or another status greater than 0 until header_sent is greater than 0. gf_sentheader will be called when status = 200, or 404. Then the send header will set to 1 letting the subsequent calls of write_callback that the header has already been sent to the client so don’t call it again. In hindsight, this loop should’ve also checked whether or not the status is greater than -1 so it can check whether or not the status was changed in the header callback. But it worked out.
Once the header has been sent to the client, it becomes straightforward. Each call of the write-callback will loop until the data passed into the write-callback is completely sent to the client by gf_sent. Each call of the write_callback will return the size of the passed data.
The flow of control within hand_with_curl
1.
Request_data, curl, gets initiated.
2.
CURLOP_WRITEFUNCTION, WRITEDATA, HEADERFUNCTION, HEADERDATA gets set.
3.
Curl_easy_perform gets called:
1.
Header_callback gets called and will parse the headers and store the relevant data in the “request_data” struct. Will be called multiple times until all the header is sent
2.
Write_callback will get called multiple times by easy_perform.
1.
Loop indefinitely until request_data->header_sent is greater than 1. (Less than 1 means the header has not been sent yet)
1.
If header_sent is less than 1, check the request_data->status.
1.
If status == 200, call gfs_sendheader(i->context, GF_OK, i->context->file_len). header_sent = 1.
2.
If status == 400, call gfs_sendheader(i->context, GF_FILE_NOT_FOUND, 0). header_sent = 1.
3.
Otherwise, return 0 to tell easy_easy to stop calling the write callback. header_sent = 1.
3.
The data passed into the write-callback is called until all of the data is sent to the client through gf_send.
4.
Return the size of the data.
Testing:
I tested the program part by part. First, I test the header_callback. By printing out the status and file length of the parsed header. Ensuring that the callback parsed the header correctly. I tested this manually by changing the workload file to contain invalid paths that are invalid then calling the gfclient_download. Once the header_callback works, I then test the write_callback in much of the same way by changing the workload to contain invalid paths. I check if all the data is sent by tracking the bytes sent vs the file_length. Make sure the header is sent correctly by printing out the status of the header. I manually called the gfclient_download and checked the console to ensure the correct output.
External Material:
Libcurl documentation: https://curl.se/libcurl/c/libcurl.html
Part 2
This part of the project focuses on using shared memory to transfer the data from the cache to the proxy server which then transfers that data to the client.
Implementation:
This part has a heavy focus on the data channel which is established using shared memory. The shared memory leaks into other implementations so I would start with the implementation of the shared memory.
shm_channel.c
The shared memory channels are initialized in webproxy.c and it returns as a steque_t. I will get into why it returns as a steque_t later. For now, I will explain how it is being initialed. In my shared memory API. shm_init function takes in the number of segments and segment size which determines how many segments and the size of each segment.
The functions loop up to the segment count, and each loop creates two struct.
Shm_info:
1.
The shm_ptr
2.
The name of the channel
3.
Shared memory file description
Share_data_t:
1.
File_length: Total file Length
2.
Chunk_size: Data of the size sent from the cache.
3.
Header Status: Status of the request
4.
Header send: Has the header been sent
5.
The current mode (“reading” or “write”): Used to tell cache and handle_with_cache whose turn it is for the the mutex.
6.
BUFSIZE: the remaining size of the buffer after accounting for all the variables in the struct
7.
Mutex: will be used by the cache and handle_with_curl
8.
Mutex Condition ready_to_write: notify the cache that it’s ready to write
9.
Mutex Condition ready_to_write: notify the handle that it’s ready to read
10.
Buffer[]: A flexible array member where the cache will write data and hand will read the data. (geeksforgeeks.org)
I will be using POSIX shared memory because after some research it seems a bit easier to implement compared to SYSV. The loop continues until all the shared memory channels are created. The name of the shared memory channel is concatenated from “/SHMSEG” and the number of the current loop. The shared message channel is then initiated following the guide published by Logan Chien. The name of the channel, the file description, and the pointer are each stored in the shm_info struct. The data within Shm_info is initiated so that it can allow for easier clean-up later, and easy access to the name for the shared memory channel through the command channel.
The shared memory pointer is a struct of share_data_t. Variable 1 – 5 of share_data_t is initiated by another function called “init_share_data” in my shared memory API. Which sets variables 1- 5 = 0. This function is important later on in the handle function to reset the pointer after the thread is done using this channel. The BUFSIZE (variable 6) of the buffer is calculated by
subtracting the segsize from the size of the share_data_t. This is the size of the buffer and is used to make sure we are within the segsize of the memory channel. The mutex, and mutex conditions are initialized in the normally in the loop. (Variable 7 -9).
Once everything is initialized. The shm_info will be stored in two steque_t. Steque_t SEGLIST is used to keep track of all of the initialized shared channels so when we clean up, we can clean up all the memory channels even if we exit during the middle of the transfer process. Steque_t SEGQUEUE is what the handle will use to manage which thread gets which shared memory channel. SEGQUEUE is also what the init_share_memory function will return.
Share_memory cleanup is called when an interrupt occurs. This function pops from the SEGLIST steque_t. Uses the respective variables from the popped shm_info to unmap, close, and unlink the shared_memory. Then, destroys the mutex and mutex conditions. It repeats this until the SEGLIST is empty. Finally, it destroys SEGLIST and SEGQUEUE.
Webproxy.c
The Webproxy initialized the shared memory, it also initialized the mutex that we will be using in the handle.c to manage the distribution of the shared memory channel. As mentioned previously, the initialized share memory function returns as a queue that the webproxy will later pass into the threads. The idea of initializing the mutex is inspired by the multi-thread part of the previous project.
Webproxy passes the server, and the SEGQUEUE to the threads which the threads.
Simplecached.c
Before we get into the handle_with cache.c I want to talk about how the command channel is created. The command channel is initialized in simplecache.c, as suggested by the project, read me. I used a POSIX message queue as compared to the SYSV because it was simpler to use and more straightforward. I followed the guide to learn how to establish the message queue (James Madison University). Another reason, I used a message queue for the IPC for the command channel is because it is already thread-safe so I didn’t have to incorporate additional mutexes. This is already a complicated project so I would like to keep things as simple as possible.
The message channel takes in two things, the path of the request and the name of the shared memory channel. Those are the only two things the threads in the simplecache to process the request. The path is used to receive the file_descriptor from the simplecache API. Shm_name is used to open up the corresponding shared memory channel to the requesting server thread.
The simplecached program is a boss-worker thread pattern. The message queue is established in the main function and is passed into the worker thread. Luckily, as mentioned earlier, the message queue is thread-safe so I didn’t need to initialize additional mutex for worker threads to pull requests from the message queue.
I will talk about the remaining implementation of this program later as it goes hand in hand with the hand program.
handle_with_cache.c
The main difficulty of this program is, how do I manage the shared memory segment with the threads. What if the shared memory segment is less than the worker thread? The solution I came up with is a cycling queue of the shared memory. The thread will pop the shared memory info from the queue of shared memories. (SEGQUEUE) When it is done with the transfer of data to
the client, it will enqueue the share_memory info back to SEGQUEUE so other threads can use the share_memory channel. To properly incorporate this, I need a mutex which was already initialed in webproxy.c. When the SEGQUEUE is empty, the handle will wait until it is notified that the queue is no longer empty. When the handler is done with the shared memory channel, it will enqueue the shared memory and signal that the queue is no longer empty.
Once this thread has successfully secured a shared memory, it will send the name of the shared memory channel and path into the message queue.
Transfer of data
Now that we have established the command channel (Message queue), we have everything we need for both the handle thread and the simplecached thread to transfer the data.
The handle thread is responsible for reading from the shared memory buffer and the simplecached thread is responsible for writing into the shared memory buffer. The most difficult part of this is, that the threads need to take turns reading and writing in the buffer. We will open the shared message channel in each thread respectively based on their role.
Refer back to the share_data_t from earlier which is the shared_memory pointer struct. The “mode” will always start in writing mode. This is extremely important to reset to write mode after the handle is done with the shared memory to avoid deadlocks.
To start as messaged earlier, the shared memory will always be in write mode first, and the first thing the simplecached thread does will be to write the file_length, and status into the shared memory channel. To do this, it will lock the mutex that is part of the shared memory channel. Once, it has set the file_length and status, it will also set the mode to read mode. Unlock the socket and signal the mutex condition “ready_to_read” to the handle thread.
Now that the handle thread has the mutex, it will copy the file_length and status to local variables so we can unlock the mutex earlier. We will set the mode to writing mode, unlock the mutex, and signal the mutex condition “ready_to_write” Right after this, gf_sendheader will be called.
Once the header is sent, the handle thread and the simplecache thread will do a back and forward between writing and reading the data until all the bytes are transferred.
For the simplecache thread, while it is not in write mode, it will wait for the signal from the handle that it is ready to write. Once it has received the signal, it will write the file chunk into the buffer inside the share_data_t. The chunk size equals the bufsize variable inside share_data_t. It is important to use pread and keep track of the offset to make sure the file descriptor is thread-safe. The chunk size is also passed into the chunk variable inside the share_data_t so that the handle thread knows the chunk size for the gf_send(). After all of this set. Mode will go back to read mode, unlock and mutex, and signal it is ready to read.
The handle thread does the exact opposite. It will wait for the signal from simplecached thread that it is ready to read. Once it has received the signal, it calls gf_send where the data is the buffer of the shared memory channel, and the chunk size is the previous set chunk size. The mode is set to write mode. It will then unlock the mutex and signal that it is ready to write.
During this back and forth, both threads will keep track of the bytes transferred separately. And continue until all the bytes are transferred.
The reason the header wasn’t sent during this back-and-forth is that I thought it would be simpler to implement if the header was set separately. It would be more straightforward to call
gf_sendheader if I know exactly when the header is sent and I can avoid an additional if statement.
The flow of control within hand_with_curl
1) Shared memory is initialized in webproxy.c, and passed the necessary information to the handle_with_cache.c thread. (SEGQUEUE, and server)
2) Message queue is initialized in simplecached.c
3) handle_with_cache pops shm_info from SEGQUEUE. Or wait if the queue is empty.
4) handle_with_cache.c passes the path and the shared memory channel name through the message queue. Since the initial mode will always be in write mode, the mutex will wait until it gets the signal that it is ready to read
5) simplecached.c received the message, opened the shared message channel, and retrieved the file descriptor from the final. Since the share message channel pointer mode is already in write mode, it will immediately secure the mutex and write the file_length and status in the responsible variables inside the shared channel pointer. It will then set the pointer mode to read. Unlock the mutex and signal the mutex cond “ready to read”. It will then wait to obtain the mutex to start writing into the buffer.
6) handle_with_cache.c will secure the mutex and copy the file_length and status into local variables. Set mode to write. Unlock the mutex, and signal “ready_to_write”
7)Simplecached.c will obtain the mutex and use pread to store the chunk to the shared memory channel pointer’s buffer. Set the chunk size of the shared memory pointer. Set mode to reading mode. Unlock the mutex and signal “ready_to_read”
8) handle_with_cache.c will secure the mutex. And call the gf_send(). With the data being the shared memory buffer and size being the shared memory buffer chunk.
9) repeat 7 and 8 until all the bytes are sent.
10) handle_with_cache.c will reset the share memory channel pointer’s variables by calling init_share_data, and enqueue the shm_info struct that it popped from step 3. And signal “queueNEmpty”.
11) repeat steps 3 – 10 until the programs are closed by the user.
Testing
For the testing, I tested my programs manually and part by part. The first part that I implemented was the message queue. So for proxy server to “complete” the request I temporarily set the handle_with_cache to return sendheader(file_not_found, 0). I made sure that the message queue was able to receive messages from the hander thread. I ran gfclient_download and made sure the simplecached would receive the correct amount of messages from the handle. I did this by manually counting shared memory names the in the console.
The next part that I implemented was the cycling of the SEGQUEUE in handle_with_cache.c. For this, I temporarily turned off the message queue part of the code so it doesn’t pass through and I could isolate the issues. For this, handle_with_cache will continue to return sendheader(file_not_found, 0) so the request can be “complete”. I ran gfclient_download multiple times to test if the queue was cycling. I should be able to run gfcleint_download as many times as I want without any issues. I monitored the console to print out the share channel name.
Once I verify this part worked. I turned back on the message queue and implemented the shared header part. And ran the gfclient_download and modified the workload with invalid paths. Just make sure the handle_with_cache is receiving the correct header information. Once I validate that the handle is receiving the correct info, I can safely implement the send data part. I monitored the bytes sent and the file length in the console and constantly checked the downed files to see if they opened properly.
During this, I also tested to see if the programs closed properly with no memory leaks by interrupting the program in multiple scenarios. Such as close the programs as soon as they initiate the shared memory. After it cycles through the SEGQUEUE, ending the programs mid-transfer, etc.
External Material:
Flexible Array Members in a structure in C: https://www.geeksforgeeks.org/flexible-array-members-structure-c/
POSIX Shared Memory | Logan’s Note: https://logan.tw/posts/2018/01/07/posix-shared-memory/
3.6 MESSAGE PASSING With Message:
https://w3.cs.jmu.edu/kirkpams/OpenCSF/Books/csf/html/MQueues.html
