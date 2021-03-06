#include "cache.h"

int read_unpack(http_msg* incoming);
int respond_send(int sd, http_msg *incoming, map<int, Newconnect *> &list,Cache<char*,char*> * cache,fd_set *master,int *fdmax,int rc);


int main(int argc, char ** argv)
{
	int server_port, max_client, new_sd= -1,rc;
	int sd,i, fdmax =0,len=0;
	char *server_ip;
	char *temp,*temp1;
	struct sockaddr_in server_addr, client_addr;
	struct hostent* hret;
	map<int, Newconnect* > client_list;
	//map<const char*, char*,cmp_str> cache; //the key is the url
	Cache<char*,char*> *lrucache=new Cache<char*,char*>(CACHE_SIZE);
	map<int, Newconnect *>::iterator it1; 
	
	
	fd_set clientfds, master_set;  // set of all the active client sockets from which server can recieve data
	struct timeval tv;
	

	if(argc != 4)
	{
		printf("ERROR:- PLEASE use this format <./server server_ip server_port max_clents>/n");
		return -1;
	}
	
	/* Reading the command line input and storing it*/
	server_ip = argv[1];
	server_port = atoi(argv[2]);
	max_client = atoi(argv[3]);

	printf("IP = %s, port = %d, max = %d\n", server_ip, server_port, max_client); 		
	
	/* creating the socket and binding it to the sever_ip provided*/

	sd = socket(AF_INET, SOCK_STREAM, 0);   // sd is the listening socket
	if(sd == -1)
	{
		printf("ERROR:- Unable to create a socket\n");
		return -1;
	}
	
        printf("%d\n",sd);

	bzero(&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(server_port);     // convert it into network byte order
	hret = gethostbyname(server_ip); 
	memcpy(&server_addr.sin_addr.s_addr, hret->h_addr, hret->h_length); 	
		
	if (-1 == bind(sd, (struct sockaddr *)&server_addr, sizeof(server_addr)) )
	{
		printf("ERROR: Unable to bind the socket to this ip = %s and port = %d\n", server_ip, server_port);
		return -1;
	}

	/* Max queue of clients request as per user input*/
	
	if( -1 == listen(sd, max_client))
	{
		printf("ERROR:- Request queue at server is above the allowed limit. Wait and try again. \n");
		return -1;
	}

	FD_ZERO(&clientfds);  // Initiliaze the client reading socket list.
	FD_ZERO(&master_set);
	FD_SET(sd, &master_set); // Include the server sd into the reading set.
	fdmax = sd;
	tv.tv_sec= 1000;  // Set the timeout value for idle case
	tv.tv_usec =0;
      	/* Server will do I/O mulitplexing using select and will continously listen */
	
	while(true)
	{	
		clientfds = master_set;	
		printf("Begin to select\n");
		rc=select(fdmax+1, &clientfds, NULL, NULL, &tv);
		
		if( -1 == rc)
		{
			printf("ERROR:- Select function failed\n");
			return -1;
		}
		if(rc==0)
		{
			printf("Now data come,I will shut down to save the power\n");
			return -1;
		}
		for(i=0; i<= fdmax; i++)
		{
		
			if(FD_ISSET(i, &clientfds))   // Some one is trying to connect to the socket i or sending some data on this socket descriptor - 'i'
			{       
                                http_msg *incoming=new http_msg;
			
				/*A new client wants to connect to the server through the listening socket */
				if(i == sd) 					
				{	
					len = sizeof(client_addr);
					new_sd = accept(sd, (struct sockaddr *)&client_addr, (socklen_t *)&len);
					if(new_sd == -1)
					{
						perror("ERROR:- Unable to connect to client\n");
					}	
					else
					{	
						FD_SET(new_sd, &master_set);
				         	if(new_sd >= fdmax) 
						fdmax = new_sd; 
					}
				 printf("\n\nNew client come!\n");

				}
				/* Some data is there to read at socket descriptor 'i' */ 
				/* Important - read and write should be made on same sd*/
				else 	
				{       
					
					
					printf("Socket %d has something to read!\n",i);
					memset(incoming->rcvd,'\0',MAX_SIZE);
					rc=read(i,incoming->rcvd,MAX_SIZE);
					printf("received %d bytes\n",rc);
					if(rc>0)
					{	
						//data from web server
						if(client_list[i]&&client_list[i]->buf_len>0)
						{
						memcpy(client_list[i]->buf+client_list[i]->buf_len,incoming->rcvd,rc);
						client_list[i]->buf_len+=rc;	
						}
						else//data for get req Or the these other segments data(Not the first segment)
						{				
						read_unpack(incoming);
						respond_send(i, incoming,client_list,lrucache,&master_set,&fdmax,rc);
						}
					}
					else if(rc==0)
					{     
						printf("socket %d's  connection closed\n",i);
						
						if(client_list[i])
						{
							if(client_list[i]->req_type==GETREQ)
							{
							//printf("recvied data for the url:\n\n\n%s\n\n",client_list[i]->buf);
							write(client_list[i]->csd,client_list[i]->buf,client_list[i]->buf_len);
							printf("SEND SIZE:%d\n",client_list[i]->buf_len);
							
							//add data into cache
							/*key*/
							temp=new char[strlen(client_list[i]->url)+1];
							memset(temp,'\0',strlen(client_list[i]->url)+1);
							memcpy(temp,client_list[i]->url,strlen(client_list[i]->url));
							
							/*data*/
							temp1=new char[client_list[i]->buf_len+1];
							memset(temp1,'\0',client_list[i]->buf_len+1);
							memcpy(temp1,client_list[i]->buf,client_list[i]->buf_len+1);

							lrucache->put(temp,temp1,client_list[i]->buf_len);
							lrucache->update_time(temp,client_list[i]->modify,client_list[i]->expire,0);
							
							printf("URL------%s\n",client_list[i]->url);
							printf("UPDATE THE CACHE FOR URL=%s Data size: %d\n\n",temp,client_list[i]->buf_len);
							lrucache->getall();
							close(client_list[i]->csd);
							FD_CLR(client_list[i]->csd, &master_set);
							}
							FD_CLR(client_list[i]->csd, &master_set);
							delete(client_list[i]);
							it1=client_list.find(i);
							client_list.erase (it1);
						}
	
						FD_CLR(i, &master_set);
						close(i);
						
  				
						//return 0;
					}
						
					else if(rc<0)
					{
						printf("read failed\n");
						return -1;
					}
					
					
					
					// read_unpack(incoming);
					
					//respond_send(i,comingmsg,client_list);
				}
						
                                         delete (incoming);
					
													
			} /* if fd is set*/
		}
	 }//always true
	
	close(sd);
	delete(lrucache);
	return 0;
		
}

/* Based on the Incoming message, the server will respond accordingly
* It have differenrt behaviour for different type of messages 	
*/

int respond_send(int sd, http_msg *incoming, map<int, Newconnect *> &list,Cache<char*,char*> * cache,fd_set *master,int *fdmax,int rc)
{
	int newsd=0;
	int temp=0;
	int *data_len=new int;
	char *temp1,*temp2;
	char *modify,*expire; 	
	map<int, Newconnect *>::iterator it; 

	switch(incoming->type)
	{
	
		
		case GETRQ: 
		{	
			
			temp1=new char[strlen(incoming->url)+1];
			memset(temp1,'\0',strlen(incoming->url)+1);
			strncpy(temp1,incoming->url,strlen(incoming->url));
	
			temp2=cache->get(temp1,data_len); //check the cache
			if(temp2) //we find data in the cache
			{
				modify=cache->get_mod(temp1);
				expire=cache->get_exp(temp1);
				delete(temp1);
				if((temp=checkdate(expire))>0)  //data doesn't expire
				{
			
					 printf("\nGET request for url:%s\nFound the matching data in  cache\n",temp1);
					 write(sd,temp2,*data_len);
					 delete(data_len);
					 printf("\nSent the date in  cache to client:\n\n");
					 cache->getall();
					 close(sd);
					 FD_CLR(sd,master);
		      			 break;
					//check the map from it url 
			
				}
			
			
				else  //date expire,we send head req to web server
				{	
					
					printf("The date in cache expired,need to update it\n");
					
					http_msg *request = new http_msg;
					
					//printf("add a new sd%d\n",newsd);
					printf("URL YOU ENTERED IS :%s\n",incoming->url);
					request->create_msg(incoming->url,HEAD);
					Newconnect *con2web=new Newconnect(request->hostname,80,sd);	
					newsd=con2web->create_newsd();
					printf("GET_Request:\n%s\n",request->getreq);			
					con2web->sendrq(newsd,request->getreq,incoming->url,HEADREQ);
       					delete(request);
	               			
					list[newsd]=con2web;   //add this new connect to map	
					printf("Forwards the URL %s request to web server successfully!\n",incoming->url);
					FD_SET(newsd, master);
					if(newsd >= *fdmax)
					*fdmax = newsd; 
					printf("USE %d SD TO UPDATE CACHE\n",newsd);
                			// deleteing the outgoing messag
					break;
				}
			}


			else //cannot find date in cache
			{
				
				printf("Cannot find the matching date in cache\n");
			
					
					//printf("add a new sd%d\n",newsd);
				printf("URL YOU ENTERED IS :%s\n",incoming->url);
			
				Newconnect *con2web=new Newconnect(incoming->hostname,80,sd);
				newsd=con2web->create_newsd();
				//printf("add a new sd%d\n",newsd);
	               		con2web->sendrq(newsd,incoming->rcvd,incoming->url,GETREQ);//forward request to web server
				//printf("URL11--------%s\n",incoming->url);
				
				list[newsd]=con2web;   //add this new connect to map	
				printf("Forwards the URL %s request to web server successfully!\n",incoming->url);
				FD_SET(newsd, master);
				if(newsd >= *fdmax)
				*fdmax = newsd; 
				printf("USE %d SD TO COMMUNICATE WITH WEB SERVER\n",newsd);
						// deleteing the outgoing message
				break;
			}
		}   
	
		
		case RSPN: 
		{
				
			if(list[sd]->req_type==HEADREQ)   //this is the respond for our HEAD request
			{	
							
				//printf("received Respond Data from webserver\n");
				
				temp1=new char[strlen(list[sd]->url)+1];
				memset(temp1,'\0',strlen(list[sd]->url)+1);
				strncpy(temp1,list[sd]->url,strlen(list[sd]->url));
			
				temp2=cache->get(temp1,data_len); //check the cache
				modify=cache->get_mod(temp1);
				expire=cache->get_exp(temp1);
				
				
				if(strncmp(modify,incoming->modify,24)==0&&strlen(modify)!=0)/*not be mdfified*/
				{
					
					// we only need to modified the expire time;then forwards the data to client
					cache->update_time(temp1,incoming->modify,incoming->expire,1);//'1'is a flag means we also need to change the expire date in the content
					write(list[sd]->csd,temp2,*data_len);
					delete(temp1);
					delete(data_len);
					printf("\nThe date in  cache wasn't modified and we only need to update its expire date.\n\n");
					printf("\nSent the date in  cache to client:\n\n");
					cache->getall();
					close(list[sd]->csd);   //we finished all the service to this client
					FD_CLR(list[sd]->csd, master);
					break;
				}
				else //DATA HAS BEEN MODIFIED
				{
					/*data has expired,we need to send a new get request to web server*/
					printf("The date in cache expired,need to update it\n");
					http_msg *request = new http_msg;
					printf("URL YOU ENTERED IS :%s\n",list[sd]->url);
					request->create_msg(list[sd]->url,GET);
					printf("GET_Request:\n%s\n",request->getreq);					
					Newconnect *con2web=new Newconnect(request->hostname,80,list[sd]->csd);
					newsd=con2web->create_newsd();
					con2web->sendrq(newsd,request->getreq,list[sd]->url,GETREQ);
       					delete(request);

					list[newsd]=con2web;   //add this new connect to client list;	
					printf("Forwards the URL %s request to web server successfully!\n",incoming->url);
					FD_SET(newsd, master);
					if(newsd >= *fdmax)
					*fdmax = newsd; 
					printf("USE %d SD TO UPDATE CACHE\n",newsd);
					break;

				}
			}

			else if(incoming->status>=200&&incoming->status<300)//This is the respond for our GET request
			{
		
				list[sd]->buf_len=rc;
                    		memcpy(list[sd]->buf,incoming->rcvd,rc);
			
				memcpy(list[sd]->expire,incoming->expire,strlen(incoming->expire));
				memcpy(list[sd]->modify,incoming->modify,strlen(incoming->modify));
				
				break;
				
			/*We need to read the first segment of date to know its type if err we need to drop it and send client a error msg*/
			}
			else //error msg
			{
				printf("Send the ERROR MSG TO CLIENT!\n");
				write(list[sd]->csd,incoming->errmsg,strlen(incoming->errmsg));
				close(list[sd]->csd);
				FD_CLR(list[sd]->csd, master);	
				close(sd);
				FD_CLR(sd, master);			
			}	

			break;
			
		}
		
		default:
		{	
			return -1;
		}
	}


	return 0;
}


/* It will read the SBCP packet (formed with all the rules of the protocol),
*  From the char buffer (the input packet), it will re-construct the datastructure
*  (unpack the packet) which is used in the program.*/


int read_unpack(http_msg* incoming)
{
	int i,j;
	char c[4];
	char *type;
	const char *get="GET";
	const char *respond="HTT";
	memset(c,'\0',4);
	memcpy(c,incoming->rcvd,3);
	c[3]='\0';
	type=c;
       // printf("type=%s\n",c);
	if(*type==*get)
	{
		incoming->type=GETRQ;
		//get the hostname and uri
		memset(incoming->uri,'\0',100);
		for(i=4,j=0;incoming->rcvd[i]!=' ';i++,j++)
		{
			incoming->uri[j]=incoming->rcvd[i];
			
		}
		memset(incoming->hostname,'\0',70);
		i=i+17;
		for(j=0;incoming->rcvd[i]!='\r';i++,j++)
		{
			incoming->hostname[j]=incoming->rcvd[i];
			
		}
		strcpy(incoming->url,incoming->hostname);
		strcpy(incoming->url+strlen(incoming->hostname),incoming->uri);
		
		printf("GET-REQUEST'S URL IS:%s\nURI IS:%s\nHOSTNAME IS:%s\n",incoming->url,incoming->uri,incoming->hostname);                       //here we also get the url as a key in cache map
		
		
	}
	else if(*type==*respond)
	{
		incoming->type=RSPN;
		memset(c,'\0',4);
		memcpy(c,incoming->rcvd+9,3);
		incoming->status=atoi(c);
		printf("recving data-status:%d\n",incoming->status);
		if(incoming->status>=300)
		{
			for(i=9,j=0;incoming->rcvd[i]!='\r';i++,j++)
			{
				incoming->errmsg[j]=incoming->rcvd[i];
			}
			printf("ERROR:%s\n",incoming->errmsg);		
		}
		incoming->getdate(); //get the modify date and expire date
		printf("\r\nModified date:%s;\nExpire date:%s \n",incoming->modify,incoming->expire);
						
	}
	
	return 1;
}	
		

	
		


