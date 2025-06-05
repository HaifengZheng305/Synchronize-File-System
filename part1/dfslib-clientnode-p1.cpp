#include <regex>
#include <vector>
#include <string>
#include <thread>
#include <cstdio>
#include <chrono>
#include <errno.h>
#include <csignal>
#include <iostream>
#include <sstream>
#include <fstream>
#include <iomanip>
#include <getopt.h>
#include <unistd.h>
#include <limits.h>
#include <sys/inotify.h>
#include <grpcpp/grpcpp.h>

#include "dfslib-shared-p1.h"
#include "dfslib-clientnode-p1.h"
#include "proto-src/dfs-service.grpc.pb.h"

using grpc::Status;
using grpc::Channel;
using grpc::StatusCode;
using grpc::ClientWriter;
using grpc::ClientReader;
using grpc::ClientContext;

//
// STUDENT INSTRUCTION:
//
// You may want to add aliases to your namespaced service methods here.
// All of the methods will be under the `dfs_service` namespace.
//
// For example, if you have a method named MyMethod, add
// the following:
//
//      using dfs_service::MyMethod
//


DFSClientNodeP1::DFSClientNodeP1() : DFSClientNode() {}

DFSClientNodeP1::~DFSClientNodeP1() noexcept {}

StatusCode DFSClientNodeP1::Store(const std::string &filename) {

    //connect to the proto
    ClientContext context;
    dfs_service::ResponseStatus stat;

    //set deadline
    std::chrono::system_clock::time_point deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(deadline_timeout);
    context.set_deadline(deadline);

    std::unique_ptr<ClientWriter<dfs_service::StoreRequest>> writer(service_stub->Store(&context, &stat));

    //https://en.cppreference.com/w/cpp/io/basic_ifstream
    //read file in binary mode;
    std::string WrappedPath = WrapPath(filename);
    std::ifstream file(WrappedPath, std::ios::binary);
    std::size_t bytes_sent = 0;

    if (!file) {
        std::cerr << "FILE_NOT_FOUND: " << filename << std::endl;
        return StatusCode::CANCELLED;  // or another appropriate error code
    }

    //https://stackoverflow.com/questions/20911584/how-to-read-a-file-in-multiple-chunks-until-eof-c
    // read file till the EOF
    const size_t chunksize = 2024;
    std::vector<char> buffer (chunksize);
    
    //Stream until the end of the file.
    while(!file.eof()){

        dfs_service::StoreRequest request;

        file.read(buffer.data(), buffer.size());

        request.set_filename(filename);

        request.set_data(buffer.data(), file.gcount());

        bytes_sent += file.gcount();

        if (!writer->Write(request)){
            std::cout << "SERVER UNAVAILABLE" << std::endl;
            return StatusCode::CANCELLED;
        }
    } 

    std::cout << bytes_sent << std::endl;

    file.close();

    if(!writer->WritesDone()){
         std::cout << "WriteDone Error" << std::endl;
        return StatusCode::CANCELLED;
    }

    Status status = writer->Finish();  // Status returned here indicates whether the RPC was successful

    if (!status.ok()) {
        // If the RPC failed, return the appropriate StatusCode
        if (status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED) {
            std::cout << "Request timed out. Server may be slow to start or unavailable." << std::endl;
            return StatusCode::DEADLINE_EXCEEDED;
        }
        else{
            std::cout << "RPC Failed with status: " << status.error_message() << std::endl;
            return StatusCode::CANCELLED;
        }
    }

    std::cout << "rpc complete"<< std::endl;
    return StatusCode::OK;


    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to store a file here. This method should
    // connect to your gRPC service implementation method
    // that can accept and store a file.
    //
    // When working with files in gRPC you'll need to stream
    // the file contents, so consider the use of gRPC's ClientWriter.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::NOT_FOUND - if the file cannot be found on the client
    // StatusCode::CANCELLED otherwise
    //
}


StatusCode DFSClientNodeP1::Fetch(const std::string &filename) {

    ClientContext context;
    dfs_service::FetchResponse response;

    dfs_service::FetchRequest request;
    request.set_filename(filename);
    
    std::unique_ptr<ClientReader<dfs_service::FetchResponse> > reader(service_stub->Fetch(&context, request));

    std::chrono::system_clock::time_point deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(deadline_timeout);
    context.set_deadline(deadline);

    if (std::chrono::system_clock::now() > deadline) {
        std::cout << "Deadline exceeded before starting the operation." << std::endl;
    return StatusCode::DEADLINE_EXCEEDED;
    }

    std::size_t bytes_received = 0;

    std::string WrappedPath = WrapPath(filename);

    //overwrite the file
    //Open the file to write the content to it
    std::ofstream out_file;

    // Continue reading and writing data for subsequent chunks
    if(reader->Read(&response)){
        if(response.fnf() == 1){
            std::cout << "Server File NOT FOUND" << std::endl;
            return StatusCode::NOT_FOUND;
        }
        std::cout << WrappedPath << " Opened" << std::endl;
        out_file.open(WrappedPath, std::ios::binary);
    }

    if (!out_file.is_open()) {
        std::cout << "unable to open file" << std::endl;
        return StatusCode::CANCELLED;
    }

    while (reader->Read(&response)) {
        out_file.write(response.data().c_str(), response.data().size());
        bytes_received += response.data().size();

        if (std::chrono::system_clock::now() > deadline) {
            std::cout << "Deadline exceeded before starting the operation." << std::endl;
        return StatusCode::DEADLINE_EXCEEDED;
        }
    }

    std::cout << bytes_received << std::endl;
    out_file.close();

    Status status = reader->Finish();

    if (!status.ok()) {
        std::cout << "sometihng not okay" << std::endl;
        // If the RPC failed, return the appropriate StatusCode
        if (status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED) {
            std::cout << "Request timed out. Server may be slow to start or unavailable." << std::endl;
            return StatusCode::DEADLINE_EXCEEDED;
        }else if(status.error_code() == grpc::StatusCode::NOT_FOUND){
            std::remove(WrappedPath.c_str());
            std::cout << "Server File NOT FOUND" << std::endl;
            return StatusCode::NOT_FOUND;
        }
        else{
            std::cout << "RPC Failed with status: " << status.error_message() << std::endl;
            return StatusCode::CANCELLED;
        }
    }

    std::cout << "Fetch complete" << std::endl;

    return StatusCode::OK;



    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to fetch a file here. This method should
    // connect to your gRPC service implementation method
    // that can accept a file request and return the contents
    // of a file from the service.
    //
    // As with the store function, you'll need to stream the
    // contents, so consider the use of gRPC's ClientReader.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::NOT_FOUND - if the file cannot be found on the server
    // StatusCode::CANCELLED otherwise
    //
    //
}

StatusCode DFSClientNodeP1::Delete(const std::string& filename) {

    ClientContext context;
    dfs_service::ResponseStatus stat;
    dfs_service::DeleteRequest request;

    //set deadline
    std::chrono::system_clock::time_point deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(deadline_timeout);
    context.set_deadline(deadline);

    request.set_filename(filename);

    Status status = service_stub->Delete(&context, request, &stat);

    if (!status.ok()) {
        // If the RPC failed, return the appropriate StatusCode
        if (status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED) {
            std::cout << "Request timed out. Server may be slow to start or unavailable." << std::endl;
            return StatusCode::DEADLINE_EXCEEDED;
        }else if(status.error_code() == grpc::StatusCode::NOT_FOUND){
            std::cout << "Server File NOT FOUND" << std::endl;
            return StatusCode::NOT_FOUND;
        }
        else{
            std::cout << "RPC Failed with status: " << status.error_message() << std::endl;
            return StatusCode::CANCELLED;
        }
    }

    return StatusCode::OK;

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to delete a file here. Refer to the Part 1
    // student instruction for details on the basics.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::NOT_FOUND - if the file cannot be found on the server
    // StatusCode::CANCELLED otherwise
    //

}

StatusCode DFSClientNodeP1::List(std::map<std::string,int>* file_map, bool display) {

    ClientContext context;
    dfs_service::ListResponse list;
    dfs_service::ResponseStatus stat;

    std::chrono::system_clock::time_point deadline = std::chrono::system_clock::now() + std::chrono::milliseconds(deadline_timeout);
    context.set_deadline(deadline);

    Status status = service_stub->List(&context, stat, &list);

    //add to file_map

    if (!status.ok()) {
        // If the RPC failed, return the appropriate StatusCode
        if (status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED) {
            std::cout << "Request timed out. Server may be slow to start or unavailable." << std::endl;
            return StatusCode::DEADLINE_EXCEEDED;
        }
        else{
            std::cout << "RPC Failed with status: " << status.error_message() << std::endl;
            return StatusCode::CANCELLED;
        }
    }

       for (const auto& entry : list.listmap()) {
            std::string filename = entry.first;  // File name
            const std::string& mtimeBytes = entry.second;  // mtime bytes

            // Deserialize the mtime (int64_t)
            int64_t mtime;
            memcpy(&mtime, mtimeBytes.data(), sizeof(mtime));

            // Store filename and mtime in the file_map
            (*file_map)[filename] = static_cast<int>(mtime);

            // Optionally, print the decoded file information
            if (display) {
                std::cout << "File: " << filename
                        << ", Modified: " << std::ctime(reinterpret_cast<time_t*>(&mtime)) << std::endl;
            }
        }

    return StatusCode::OK;


    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to list all files here. This method
    // should connect to your service's list method and return
    // a list of files using the message type you created.
    //
    // The file_map parameter is a simple map of files. You should fill
    // the file_map with the list of files you receive with keys as the
    // file name and values as the modified time (mtime) of the file
    // received from the server.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::CANCELLED otherwise
    //
    //
}

StatusCode DFSClientNodeP1::Stat(const std::string &filename, void* file_status) {

    //
    // STUDENT INSTRUCTION:
    //
    // Add your request to get the status of a file here. This method should
    // retrieve the status of a file on the server. Note that you won't be
    // tested on this method, but you will most likely find that you need
    // a way to get the status of a file in order to synchronize later.
    //
    // The status might include data such as name, size, mtime, crc, etc.
    //
    // The file_status is left as a void* so that you can use it to pass
    // a message type that you defined. For instance, may want to use that message
    // type after calling Stat from another method.
    //
    // The StatusCode response should be:
    //
    // StatusCode::OK - if all went well
    // StatusCode::DEADLINE_EXCEEDED - if the deadline timeout occurs
    // StatusCode::NOT_FOUND - if the file cannot be found on the server
    // StatusCode::CANCELLED otherwise
    //
    //
}

//
// STUDENT INSTRUCTION:
//
// Add your additional code here, including
// implementations of your client methods
//


