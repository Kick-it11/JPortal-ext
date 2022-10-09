#include <iostream>
#include <thread>
#include "task/worker.hpp"
#include "task/task.hpp"
#include "task/task_manager.hpp"

int Worker::work(){
    Task* task = _taskManager->getTask();
    while(task!=nullptr){
        task = task->doTask();
        if(task==nullptr){
            task = _taskManager->getTask();
        }
    }
    return 0;
}