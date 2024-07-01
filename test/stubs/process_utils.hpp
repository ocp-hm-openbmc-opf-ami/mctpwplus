#pragma once

#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <functional>
#include <iostream>
#include <memory>
#include <span>
#include <stdexcept>
#include <thread>

class Task
{
  public:
    virtual void task() = 0;
    virtual void onData(std::span<uint8_t>) = 0;
};

class Process
{
  public:
    Process(std::shared_ptr<Task> t) : task(t)
    {
        if (!task)
        {
            throw std::runtime_error("Invalid task");
        }
        if (pipe(pipeHandles))
        {
            throw std::runtime_error("Failed to create pipe");
        }
        pid = fork();
        auto closeHandle = pid == 0 ? 1 : 0;
        close(pipeHandles[closeHandle]);
    }

    bool isChildProcess()
    {
        return pid == 0;
    }

    int run()
    {
        if (pid == 0)
        {
            pipeReadThread =
                std::thread(std::bind(&Process::pipeReadTask, this));
            task->task();
        }
        return pid;
    }

    void killProcess()
    {
        if (pid != 0)
        {
            kill(pid, SIGKILL);
        }
    }

    int wait()
    {
        int status = 0;
        int waitStatus = 0;
        if (pid > 0)
        {
            waitStatus = waitpid(pid, &status, 0);
        }
        if (pid == 0 || waitStatus < 0)
        {
            throw std::runtime_error("Wait failed");
        }
        if (status != 0)
        {
            std::cerr << "Wait status " << status << '\n';
        }
        return status;
    }

    int writeFD()
    {
        return pipeHandles[1];
    }

    int writeData(std::span<uint8_t> data)
    {
        // std::cout << "Writing to " << pipeHandles[1] << ' ' <<
        // data.size_bytes() << " bytes" << '\n';
        return write(pipeHandles[1], data.data(), data.size_bytes());
    }

    ~Process()
    {
        isClosed = true;
        auto closeHandle = pid == 0 ? 0 : 1;
        close(pipeHandles[closeHandle]);
        // std::cout << "Closing " << closeHandle << " " << getpid() << '\n';

        if (pid == 0)
        {
            // std::cout << "Waiting for thread" << '\n';
            if (pipeReadThread.joinable())
            {
                pipeReadThread.join();
            }
            // std::cout << "Waiting for thread done" << '\n';
        }
    }

  protected:
    void pipeReadTask()
    {
        std::array<uint8_t, 128> buffer;
        ssize_t bytesRead;

        // std::cout << "Reading from pipe starting " << pipeHandles[0] << '\n';
        while (!isClosed)
        {
            fd_set rfds;
            struct timeval tv;
            int retval;

            FD_ZERO(&rfds);
            FD_SET(pipeHandles[0], &rfds);
            tv.tv_sec = 0;
            tv.tv_usec = 100 * 1000;
            retval = select(pipeHandles[0] + 1, &rfds, NULL, NULL, &tv);

            if (retval == -1)
            {
                std::cout << "Error on select ";
                break;
            }
            else if (retval && !isClosed)
            {
                // std::cout << "Data available " << retval << " isclosed " <<
                // isClosed << '\n';
                bytesRead = read(pipeHandles[0], buffer.data(), buffer.size());
                if (bytesRead > 0)
                {
                    std::span<uint8_t> dataSpan(buffer.begin(), bytesRead);
                    task->onData(dataSpan);
                }
            }
            else
            {
                // std::cout << "No data " << retval << '\n';
            }
        }
    }
    std::shared_ptr<Task> task;
    pid_t pid = 0;
    int pipeHandles[2];
    std::thread pipeReadThread;
    std::atomic<bool> isClosed = false;
};