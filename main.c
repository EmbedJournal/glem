/***************************************************************************
 * This program is free software: you can redistribute it and/or modify    *
 * it under the terms of the GNU General Public License as published by    *
 * the Free Software Foundation, either version 3 of the License, or       *
 * (at your option) any later version.                                     *
 *                                                                         *
 * This program is distributed in the hope that it will be useful,         *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of          *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the           *
 * GNU General Public License for more details.                            *
 *                                                                         *
 * You should have received a copy of the GNU General Public License       *
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.   *
 *                                                                         *
 **************************************************************************/

/*
 *        File: glem/main.c
 *  Created on: 07-May-2016
 *      Author: Siddharth Chandrasekaran
 *        Mail: siddharth3141@gmail.com
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <signal.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/prctl.h>

#include <GL/glut.h>
#include <GL/gl.h>

// Unix socket location
#define ADDRESS			"/tmp/glcdSocket"

// On high res monitors, a small GLCDs may look very
// small. Scale factor is used to scale up the size.
// In this case it is scaled up 4 times.
#define SCALE_FACTOR		4

// Internal Macros
#define glcd_get_pixel(a,x,y) (a[y * (glcd_width / 8) + (x / 8)] & (1 << (7 - x % 8)));
#define convert_local_to_glut(x,y) do { x = window_origin_x + (x * scale_factor); \
	y = window_origin_y + (y * scale_factor); } while(0)

// Globals
uint8_t *glcd_frame;
uint8_t *glut_buf;
int server_sock_fd;

int glcd_buf_len;
int glcd_width;
int glcd_height;
int scale_factor;
int window_origin_x;
int window_origin_y;
int gl_width;
int gl_height;

void glut_set_pixel(int x, int y, int color)
{
	int i=0;
	for(i=0; i < 3; i++ ) {  //RGB Mirrors
		glut_buf[(y*gl_width+x)*3+i] = color;
	}
}

void glut_set_scaled_pixel(int x, int y, int color)
{
	int i, j, setVal;
	setVal = color ? 255 : 0;
	convert_local_to_glut(x, y);
	for (i=x; i<(x+scale_factor); i++) {
		for (j=y; j<(y+scale_factor); j++) {
			glut_set_pixel(i, j, setVal);
		}
	}
}

void make_glut_buf()
{
	int i, j, pix;
	for (i=0; i<glcd_width; i++) {
		for (j=0; j<glcd_height; j++) {
			pix = glcd_get_pixel(glcd_frame, i, j);
			glut_set_scaled_pixel(i, glcd_height-1-j, pix);
		}
	}
}

void glut_draw_screen()
{
	make_glut_buf();
	glDrawPixels(gl_width, gl_height, GL_RGB, GL_UNSIGNED_BYTE, glut_buf);
	glutSwapBuffers();
	glutPostRedisplay();
}

void reshape(int x, int y)
{
	glViewport(0, 0, (GLsizei) gl_width+100, (GLsizei) gl_height);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0.0, gl_width+100, 0.0, gl_height, -1.f, 1.f);
}

void draw_rect_thick(int x, int y, int w, int h, int tx, int ty, int color)
{
	int i, t;

	if (tx == 0) {
		tx = 1;
	}

	if (ty == 0) {
		ty = 1;
	}

	for (i=x; i<x+w; i++) {
		/* Top and bottom sides */
		for (t=0; t<(ty); t++) {
			glut_set_pixel(i, y+t, color);
			glut_set_pixel(i, y+h-1-t, color);
		}
	}
	for (i=y; i<y+h; i++) {
		/* Left and right sides */
		for (t=0; t<(tx); t++) {
			glut_set_pixel(x+t, i, color);
			glut_set_pixel(x+w-1-t, i, color);
		}
	}
}

void glut_sig_handler(int sigNum)
{
	printf("GLUT sig handler\n");
	shutdown(server_sock_fd, SHUT_RDWR);
	close(server_sock_fd);
	unlink(ADDRESS);
	exit(0);
}

void glem_sig_handler(int sigNum)
{
	printf("GLEM sig handler\n");
	exit(0);
}

void glem_sig_chld_handler(int sigNum)
{
	printf("CHD: PPID[%d] PID[%d]\n", getppid(), getpid());
	//exit(0);
}

void glut_init(int *argc, char **argv)
{
	int pid;
	glcd_frame = mmap(NULL, sizeof(uint8_t)*glcd_buf_len,
			PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (glcd_frame == NULL) {
		printf("GLCD alloc Failed!!\n");
		exit(-1);
	}
	pid = fork();
	if(pid != 0) {
		// parent process.
		return;
	}
	printf("MN: PPID[%d] PID[%d]\n", getppid(), getpid());
	glut_buf = malloc(sizeof(uint8_t)*gl_width*gl_height*3);
	if (glut_buf == NULL) {
		printf("GLUT buf alloc failed\n");
		exit(-1);
	}
	// This thread reads from glcdFrame buffer and writes it 
	// into the GLUT window. This is a pure consumer.
	prctl(PR_SET_PDEATHSIG, SIGINT);
	signal(SIGINT, glut_sig_handler);
	glutInitWindowSize(gl_width+100, gl_height+100);
	glutInit(argc, argv);
	glutReshapeFunc(reshape);
	glutIdleFunc(glut_draw_screen);
	glutInitDisplayMode(GLUT_RGB | GLUT_DOUBLE);
	glutCreateWindow("GLCD Emulator v0.01");
	glClearColor(1.0, 1.0, 1.0, 0.0);
	glColor3f(255.0, 255.0, 255.0);
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glOrtho(0.0, 1.0, 0.0, 1.0, -1.0, 1.0);
	glClear(GL_POINT_BIT);
	draw_rect_thick(window_origin_x-2, window_origin_y-2, gl_width+4, gl_height+4, 2, 2, 255);
	glutMainLoop();	// This is blocking!
	printf("Got out of glutMainLoop()");
	exit (0);	// Should never get here.
	
}

void print_usage()
{
	printf("Usage: glem -w GLCD_WIDTH -h GLCD_HEIGHT -s SCALE_FACTOR\n");
}

int main(int argc, char *argv[])
{
	signal(SIGINT, glem_sig_handler);
	signal(SIGCHLD, glem_sig_chld_handler);
	printf("MA: PPID[%d] PID[%d]\n", getppid(), getpid());
	if (argc < 4) {
		print_usage();
		exit(-1);
	}
	int opt;
	while ((opt = getopt(argc, argv, "w:h:s:")) != -1) {
		switch(opt) {
		case 'w': 
			glcd_width = atoi(optarg);
			break;
		case 'h':
			glcd_height = atoi(optarg);
			break;
		case 's':
			scale_factor = atoi(optarg);
			break;
		default:// '?'
			printf("Invalid arguement!\n");
			print_usage();
			exit(-1);
		}
	}
	gl_width = glcd_width*scale_factor;
	gl_height = glcd_height*scale_factor;
	glcd_buf_len = glcd_width/8*glcd_height;
	window_origin_x = ((100+gl_width)/2)-(gl_width/2);
	window_origin_y = ((100+gl_height)/2)-(gl_height/2);
	printf("GL(%d,%d) - GLCD(%d,%d), - O(%d,%d) - SD:%d\n", gl_width, gl_height,
		glcd_width, glcd_height, window_origin_x, window_origin_y, scale_factor);
	glut_init(&argc, argv);		// may modify argc and argv.
	if ((server_sock_fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		perror("server: socket");
		exit(1);
	}

	struct sockaddr_un sockServ;
	sockServ.sun_family = AF_UNIX;
	strcpy(sockServ.sun_path, ADDRESS);
	unlink(ADDRESS);

	socklen_t sockLen = sizeof(sockServ.sun_family) + strlen(sockServ.sun_path);
	if (bind(server_sock_fd, (const struct sockaddr *)&sockServ, sockLen) < 0) {
		perror("server: bind");
		exit(1);
	}
	if (listen(server_sock_fd, 5) < 0) {
		perror("server: listen");
		exit(1);
	}

	while (1) {
		int client_fd, rec;
		struct sockaddr_un clientSock;
		socklen_t clientLen = sizeof(clientSock);
		if ((client_fd = accept(server_sock_fd, (struct sockaddr *)&clientSock, &clientLen)) < 0) {
			perror("server: accept");
			exit(1);
		}
		uint8_t tmp[10*1000];
		if ((rec = read(client_fd, tmp, glcd_buf_len)) < 0) {
			perror("[ ! ] ERROR reading from socket");
			exit(-1);
		}
		printf("Rec: %d bytes from client\n", rec);
		// Race condition: The consumer theread may have been
		// reading when we call memcpy here causing tear. But
		// that's something we can't live with.
		memcpy(glcd_frame, tmp, glcd_buf_len);
		close(client_fd);
	}
	// close left here for sytactic sugar.
	// the socket is closed in signal handler on exit.
	close(server_sock_fd);
	return 0;
}

