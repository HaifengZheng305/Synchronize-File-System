#include <map>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <errno.h>
#include <iostream>
#include <fstream>
#include <getopt.h>
#include <dirent.h>
#include <sys/stat.h>
#include <grpcpp/grpcpp.h>

#include "src/dfs-utils.h"
#include "dfslib-shared-p1.h"
#include "dfslib-servernode-p1.h"
#include "proto-src/dfs-service.grpc.pb.h"

using grpc::Status;
using grpc::Server;
using grpc::StatusCode;
using grpc::ServerReader;
using grpc::ServerWriter;
using grpc::ServerContext;
using grpc::ServerBuilder;

using dfs_service::DFSService;


//
// STUDENT INSTRUCTION:
//
// DFSServiceImpl is the implementation service for the rpc methods
// and message types you defined in the `dfs-service.proto` file.
//
// You should add your definition overrides here for the specific
// methods that you created for your GRPC service protocol. The
// gRPC tutorial described in the readme is a good place to get started
// when trying to understand how to implement this class.
//
// The method signatures generated can be found in `proto-src/dfs-service.grpc.pb.h` file.
//
// Look for the following section:
//
//      class Service : public ::grpc::Service {
//
// The methods returning grpc::Status are the methods you'll want to override.
//
// In C++, you'll want to use the `override` directive as well. For example,
// if you have a service method named MyMethod that takes a MyMessageType
// and a ServerWriter, you'll want to override it similar to the following:
//
//      Status MyMethod(ServerContext* context,
//                      const MyMessageType* request,
//                      ServerWriter<MySegmentType> *writer) override {
//
//          /** code implementation here **/
//      }
//
class DFSServiceImpl final : public DFSService::Service {

private:

    /** The mount path for the server **/
    std::string mount_path;

    /**
     * Prepend the mount path to the filename.
     *
     * @param filepath
     * @return
     */
    const std::string WrapPath(const std::string &filepath) {
        return this->mount_path + filepath;
    }


public:

    DFSServiceImpl(const std::string &mount_path): mount_path(mount_path) {
    }

    ~DFSServiceImpl() {}

    //
    // STUDENT INSTRUCTION:
    //
    // Add your additional code here, including
    // implementations of your protocol service methods
    //

    Status Store(ServerContext* context, ServerReader<dfs_service::StoreRequest>* reader, dfs_service::ResponseStatus* stat) override {

         
        dfs_service::StoreRequest request;
        std::ofstream out_file;
        std::size_t bytes_received = 0;

        if (context->IsCancelled()) {
            return Status(StatusCode::DEADLINE_EXCEEDED, "Deadline exceeded or Client cancelled, abandoning.");
        }

        
        // Read the first chunk to get the filename and overwrite the file
        if (reader->Read(&request)) {
            // Extract filename from the first request
            std::string filename = WrapPath(request.filename());

            std::cout << "Started receiving file: " << filename << std::endl;

            // Process the first chunk of data

            out_file.open(filename, std::ios::binary);

            if (!out_file.is_open()) {
                stat->set_status("FAILED");
                return grpc::Status(grpc::StatusCode::INTERNAL, "Unable to open file.");
            }

            out_file.write(request.data().c_str(), request.data().size());
            bytes_received += request.data().size();
        }
            
        // Continue reading and writing data for subsequent chunks
        while (reader->Read(&request)) {
            out_file.write(request.data().c_str(), request.data().size());
            bytes_received += request.data().size();
        }

        std::cout << bytes_received << std::endl;
        out_file.close();


        stat->set_status("Success");

        return Status::OK;
    
    }

    Status Fetch(ServerContext* context, const dfs_service::FetchRequest* request, ServerWriter<dfs_service::FetchResponse>* writer) override{
        
        if (context->IsCancelled()) {
            return Status(StatusCode::DEADLINE_EXCEEDED, "Deadline exceeded or Client cancelled, abandoning.");
        }

        std::string WrappedPath = WrapPath(request->filename());

        std::ifstream file(WrappedPath, std::ios::binary);
        std::size_t bytes_sent = 0;

        if (!file) {
            std::cout << "file " << request->filename() << " not found.\n"<<std::endl;
            dfs_service::FetchResponse response;
            response.set_fnf(1);

            if (!writer->Write(response)){

                std::cout << "RPC FAIL "<<std::endl;
                return grpc::Status(grpc::StatusCode::INTERNAL, "RPC FAIL");
            }
            
            return grpc::Status(grpc::StatusCode::NOT_FOUND, "File not found.");
        }
        else{
            std::cout << "file " << request->filename() << " found.\n"<<std::endl;
            dfs_service::FetchResponse response;
            response.set_fnf(0);
            if (!writer->Write(response)){
                std::cout << "RPC FAIL "<<std::endl;
                return grpc::Status(grpc::StatusCode::INTERNAL, "RPC FAIL");
            }
        }

        const size_t chunksize = 2024;
        std::vector<char> buffer (chunksize);

        while(!file.eof()){

            dfs_service::FetchResponse response;

            file.read(buffer.data(), buffer.size());

            response.set_data(buffer.data(), file.gcount());

            bytes_sent += file.gcount();

            if (!writer->Write(response)){
                std::cout << "RPC FAIL "<<std::endl;
                return grpc::Status(grpc::StatusCode::INTERNAL, "RPC FAIL");
            }
        } 


        return Status::OK;
    }

    Status Delete(ServerContext* context, const dfs_service::DeleteRequest* request, dfs_service::ResponseStatus* status) override {
        //https://stackoverflow.com/questions/12774207/fastest-way-to-check-if-a-file-exists-using-standard-c-c11-14-17-c
        //check if file exist
        struct stat buffer;   
        std::string filename = WrapPath(request->filename());

        if (context->IsCancelled()) {
            return Status(StatusCode::DEADLINE_EXCEEDED, "Deadline exceeded or Client cancelled, abandoning.");
        }

        if (stat(filename.c_str(), &buffer) != 0){
            std::cout << "file " << request->filename() << " not found.\n"<<std::endl;
            return grpc::Status(grpc::StatusCode::NOT_FOUND, "File not found.");
        }

        //https://www.geeksforgeeks.org/how-to-delete-a-file-in-cpp/
        //remove file

        int remove = std::remove(filename.c_str());

        if (remove != 0){
            perror("Error deleting file");
            return grpc::Status(grpc::StatusCode::INTERNAL, "Unable to delete file.");
        }

        std::cout << "remove successful " << request->filename() << std::endl;
        return Status::OK;
    }
    Status List(ServerContext* context, const dfs_service::ResponseStatus* request, dfs_service::ListResponse* list) override {
        DIR* dir = opendir(mount_path.c_str());
        if (dir == nullptr) {
            perror("opendir");
            return Status(StatusCode::CANCELLED, "directory don't exist");
        }

        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {

            if (std::strcmp(entry->d_name, ".") == 0 || std::strcmp(entry->d_name, "..") == 0 || std::strcmp(entry->d_name, ".gitignore") == 0) {
                continue;
            }
            std::string WrappedPath = WrapPath(entry->d_name);
            struct stat fileStat;

            if (stat(WrappedPath.c_str(), &fileStat) == 0) {
                 std::string filename = entry->d_name;

                int64_t mtime = static_cast<int64_t>(fileStat.st_mtime);
                std::string mtimeBytes(reinterpret_cast<const char*>(&mtime), sizeof(mtime));

                std::cout << "File: " << entry->d_name << ", Modified: " << std::ctime(&fileStat.st_mtime); // Print modification time

                (*list->mutable_listmap())[filename] = mtimeBytes;
            }
        }

        closedir(dir);

        return Status::OK;
    }


};

//
// STUDENT INSTRUCTION:
//
// The following three methods are part of the basic DFSServerNode
// structure. You may add additional methods or change these slightly,
// but be aware that the testing environment is expecting these three
// methods as-is.
//
/**
 * The main server node constructor
 *
 * @param server_address
 * @param mount_path
 */
DFSServerNode::DFSServerNode(const std::string &server_address,
        const std::string &mount_path,
        std::function<void()> callback) :
    server_address(server_address), mount_path(mount_path), grader_callback(callback) {}

/**
 * Server shutdown
 */
DFSServerNode::~DFSServerNode() noexcept {
    dfs_log(LL_SYSINFO) << "DFSServerNode shutting down";
    this->server->Shutdown();
}

/** Server start **/
void DFSServerNode::Start() {
    DFSServiceImpl service(this->mount_path);
    ServerBuilder builder;
    builder.AddListeningPort(this->server_address, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    this->server = builder.BuildAndStart();
    dfs_log(LL_SYSINFO) << "DFSServerNode server listening on " << this->server_address;
    this->server->Wait();
}

//
// STUDENT INSTRUCTION:
//
// Add your additional DFSServerNode definitions here
//
