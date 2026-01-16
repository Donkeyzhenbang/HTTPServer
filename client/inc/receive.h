#ifndef __RECEIVE_H_
#define __RECEIVE_H_
void StartReadThread(int* pSocketId);
int AutoGetPhotoHander(const char* filename, int channelNo, int SocketFd);
int waitForHeartBeat(int fd);
int waitForB341(int fd);
int waitForB351(int fd);
#endif // !1

