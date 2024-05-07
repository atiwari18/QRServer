PROJECT 2 - QR CODE SERVER
---------------------------

This project comes with 2 folders one for the server and one for the client. 

NOTE: IT IS IMPORTANT TO RUN THE QRServer executable PRIOR TO INITIATING ANY CONNECTION WITH THE CLIENT. This ensures that the program runs smoothly. 

In order to compile the code please run `make clean` and then `make all` or make to compile the program. 

Next, run ./QRServer in one terminal to start the with appropriate flags as desired. For the run ./client in the terminal to start the client. 

In the QRServer window as connections are made these will be displayed in the terminal there is also a log file that can be retrieved from the server folder after the program has finished executing. 

Although it has not been tested, the code currently works with local clients but due to the functionality it can be scaled to a local network, but nothing beyond that. 

====================================================================================================
On the client side there will be a few features to take note of:
====================================================================================================
In order to send a server a request follow this protocol (MAKE SURE THAT THE IMAGE IS SAVED IN A .PNG FORMAT IN THE CLIENT FOLDER):
atharva@atharva-VirtualBox:~/Project_2/Client$ ./client 2012 --> DEFAULT PORT IS 2012 BUT THIS STILL MUST BE ENTERED WHEN CALLING THE CLIENT, FOR REASONS I AM TRYING TO RESOLVE. 
Enter the path to the QR code image file (or enter 'q' to quit): ./QR_2.png


If the client surpasses the threshold of max_users that client will still be prompted to send an image to the server but once a message is sent it is ignored and a "Server is busy" message is displayed. This functionality is the same with regards to timeouts as well. 
A few examples can be seen below:

atharva@atharva-VirtualBox:~/Project_2/Client$ ./client 2012
Enter the path to the QR code image file (or enter 'q' to quit): ./QR_2.png
Server response: Timeout. Connection closed.

atharva@atharva-VirtualBox:~/Project_2/Client$ ./client 2012
Enter the path to the QR code image file (or enter 'q' to quit): ./QR-2.png
Error opening file: No such file or directory
Please try again.
Enter the path to the QR code image file (or enter 'q' to quit): QR_2.png
Server response: Server is busy. Please try again later.

====================================================================================================
On the SERVER side there will be a few features to take note of:
====================================================================================================
After the program has been executed there is a file titled: server_log.txt that has logged all the activity that has occured within a particular server session. 

To conclude a server session using CTRL+C is sufficient to condlude a session. But make sure to 'q' (quit) from the clients first otherwise the ports will still be occupied. 

If there are any issues with regards to concurrency please end the session and try again and if the issues persist please contact me. 
