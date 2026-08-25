#include<bits/stdc++.h>
using namespace std;
struct Engine{
    GLFWwindow* window;
    int WIDTH=800;
    int HEIGHT=600;
    float width=1e11;
    float height=7.5e10;

    Engine(){
        if(!glfwInit()){
            window=glfwCreateWindow(WIDTH,HEIGHT,"Black Hole sim",NULL,NULL);
            if(!window){
                cerr<< "Window failed to create, PANIC!"<<endl;    
                glfwTerminate();
                exit(EXIT_FAILURE); 
            }
            glfwMakeContextCurrent(window);
        }
    }

    void run(){
        glClear(GL_COLOR_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION);
        double left=-width;
        double right=width;
        double bottom=-height;
        double top=height;
        glOrtho(left,right,bottom,top,-1,1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
        

        while(!glfwWindowShouldClose(window)){
            glClear(GL_COLOR_BUFFER_BIT);
            glfwSwapBuffers(window);
            glfwPollEvents();
        }
        glfwTerminate();
    }
};
int main() {
    while(!glfwWindowShouldClose(engine.window)){
        engine.run();
        glfwSwapBuffers(engine.Window);
        glfwPollEvents();
    }
}