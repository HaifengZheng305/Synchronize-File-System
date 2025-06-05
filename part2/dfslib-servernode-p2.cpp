#include <map>
#include <mutex>
#include <shared_mutex>
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

#include "proto-src/dfs-service.grpc.pb.h"
#include "src/dfslibx-call-data.h"
#include "src/dfslibx-service-runner.h"
#include "dfslib-shared-p2.h"
#include "dfslib-servernode-p2.h"

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
// Change these "using" aliases to the specific
// message types you are using in your `dfs-service.proto` file
// to indicate a file request and a listing of files from the server
//
using FileRequestType = dfs_service::FileRequest;
using FileListResponseType = dfs_service::FileList;

extern dfs_log_level_e DFS_LOG_LEVEL;

//
// STUDENT INSTRUCTION:
//
// As with Part 1, the DFSServiceImpl is the implementation service for the rpc methods
// and message types you defined in your `dfs-service.proto` file.
//
// You may start with your Part 1 implementations of each service method.
//
// Elements to consider for Part 2:
//
// - How will you implement the write lock at the server level?
// - How will you keep track of which client has a write lock for a file?
//      - Note that we've provided a preset client_id in DFSClientNode that generates
//        a client id for you. You can pass that to the server to identify the current client.
// - How will you release the write lock?
// - How will you handle a store request for a client that doesn't have a write lock?
// - When matching files to determine similarity, you should use the `file_checksum` method we've provided.
//      - Both the client and server have a pre-made `crc_table` variable to speed things up.
//      - Use the `file_checksum` method to compare two files, similar to the following:
//
//          std::uint32_t server_crc = dfs_file_checksum(filepath, &this->crc_table);
//
//      - Hint: as the crc checksum is a simple integer, you can pass it around inside your message types.
//
class DFSServiceImpl final :
    public DFSService::WithAsyncMethod_CallbackList<DFSService::Service>,
        public DFSCallDataManager<FileRequestType , FileListResponseType> {

private:

    /** The runner service used to start the service and manage asynchronicity **/
    DFSServiceRunner<FileRequestType, FileListResponseType> runner;

    /** The mount path for the server **/
    std::string mount_path;

    /** Mutex for managing the queue requests **/
    std::mutex queue_mutex;

    /** The vector of queued tags used to manage asynchronous requests **/
    std::vector<QueueRequest<FileRequestType, FileListResponseType>> queued_tags;

    /** Map that is keeping track of the who currently has the write access**/

    std::map<std::string, std::string> access_map;

    /** Mutex for managing the access_map **/

    std::mutex access_mutex;

    /**
     * Prepend the mount path to the filename.
     *
     * @param filepath
     * @return
     */
    const std::string WrapPath(const std::string &filepath) {
        return this->mount_path + filepath;
    }

    /** CRC Table kept in memory for faster calculations **/
    CRC::Table<std::uint32_t, 32> crc_table;

public:

    DFSServiceImpl(const std::string& mount_path, const std::string& server_address, int num_async_threads):
        mount_path(mount_path), crc_table(CRC::CRC_32()) {

        this->runner.SetService(this);
        this->runner.SetAddress(server_address);
        this->runner.SetNumThreads(num_async_threads);
        this->runner.SetQueuedRequestsCallback([&]{ this->ProcessQueuedRequests(); });

    }

    ~DFSServiceImpl() {
        this->runner.Shutdown();
    }

    void Run() {
        this->runner.Run();
    }

    /**
     * Request callback for asynchronous requests
     *
     * This method is called by the DFSCallData class during
     * an asynchronous request call from the client.
     *
     * Students should not need to adjust this.
     *
     * @param context
     * @param request
     * @param response
     * @param cq
     * @param tag
     */
    void RequestCallback(grpc::ServerContext* context,
                         FileRequestType* request,
                         grpc::ServerAsyncResponseWriter<FileListResponseType>* response,
                         grpc::ServerCompletionQueue* cq,
                         void* tag) {

        std::lock_guard<std::mutex> lock(queue_mutex);
        this->queued_tags.emplace_back(context, request, response, cq, tag);

    }

    /**
     * Process a callback request
     *
     * This method is called by the DFSCallData class when
     * a requested callback can be processed. You should use this method
     * to manage the CallbackList RPC call and respond as needed.
     *
     * See the STUDENT INSTRUCTION for more details.
     *
     * @param context
     * @param request
     * @param response
     */
    void ProcessCallback(ServerContext* context, FileRequestType* request, FileListResponseType* response) {

        DIR* dir = opendir(mount_path.c_str());
        if (dir == nullptr) {
            perror("opendir");
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

                (*response->mutable_listmap())[filename] = mtimeBytes;
            }
        }

        closedir(dir);
        //
        // STUDENT INSTRUCTION:
        //
        // You should add your code here to respond to any CallbackList requests from a client.
        // This function is called each time an asynchronous request is made from the client.
        //
        // The client should receive a list of files or modifications that represent the changes this service
        // is aware of. The client will then need to make the appropriate calls based on those changes.
        //
        
    }

    /**
     * Processes the queued requests in the queue thread
     */
    void ProcessQueuedRequests() {
        while(true) {

            //
            // STUDENT INSTRUCTION:
            //
            // You should add any synchronization mechanisms you may need here in
            // addition to the queue management. For example, modified files checks.
            //
            // Note: you will need to leave the basic queue structure as-is, but you
            // may add any additional code you feel is necessary.
            //


            // Guarded section for queue
            {
                dfs_log(LL_DEBUG2) << "Waiting for queue guard";
                std::lock_guard<std::mutex> lock(queue_mutex);


                for(QueueRequest<FileRequestType, FileListResponseType>& queue_request : this->queued_tags) {
                    this->RequestCallbackList(queue_request.context, queue_request.request,
                        queue_request.response, queue_request.cq, queue_request.cq, queue_request.tag);
                    queue_request.finished = true;
                }

                // any finished tags first
                this->queued_tags.erase(std::remove_if(
                    this->queued_tags.begin(),
                    this->queued_tags.end(),
                    [](QueueRequest<FileRequestType, FileListResponseType>& queue_request) { return queue_request.finished; }
                ), this->queued_tags.end());

            }
        }
    }

 Status UnlockWriteAccess(ServerContext* context, const dfs_service::WriteRequest* request, dfs_service::ResponseStatus* status) override {
        if (context->IsCancelled()) {
            return Status(StatusCode::DEADLINE_EXCEEDED, "Deadline exceeded or Client cancelled, abandoning.");
        }

        std::lock_guard<std::mutex> lock(access_mutex);

        if (access_map.erase(request->filename()) == 0) {
            std::cout << "Unable to Unlock Write access for " << request->filename() << std::endl;
            grpc::Status(grpc::StatusCode::INTERNAL, "Write_Access Unlock Denied");
        } else {
            std::cout << "Unlocked Write access for " << request->filename() << std::endl;
        }

        return Status::OK;
    }

    //
    // STUDENT INSTRUCTION:
    //
    // Add your additional code here, including
    // the implementations of your rpc protocol methods.
    //

    Status WriteAccess(ServerContext* context, const dfs_service::WriteRequest* request, dfs_service::ResponseStatus* status) override {
        if (context->IsCancelled()) {
            return Status(StatusCode::DEADLINE_EXCEEDED, "Deadline exceeded or Client cancelled, abandoning.");
        }

        std::lock_guard<std::mutex> lock(access_mutex);

        if(access_map.find(request->filename()) != access_map.end()){
            //one line of code to check if it's the same client;
            std::cout << access_map[request->filename()] << " has write access" << std::endl;
            return grpc::Status(grpc::StatusCode::RESOURCE_EXHAUSTED, "Write_Access Denied");
        }

        access_map[request->filename()] = request->clientid();

        std::cout << "Write access granted to client " << request->clientid() 
              << " for file " << request->filename() << std::endl;

        return Status::OK;
    
    }

    Status Store(ServerContext* context, ServerReader<dfs_service::StoreRequest>* reader, dfs_service::ResponseStatus* response) override {

         
        dfs_service::StoreRequest request;
        std::ofstream out_file;
        std::size_t bytes_received = 0;

        std::this_thread::sleep_for(std::chrono::seconds(3));

        struct stat buffer;
        
        // Read the first chunk to get the filename and overwrite the file
        if (reader->Read(&request)) {
            std::cout << "writing header" << std::endl;
            // Extract filename from the first request
            std::string filename = WrapPath(request.filename());

            if(stat(filename.c_str(), &buffer) == 0){
                std::uint32_t client_crc = request.crc();

                std::uint32_t server_crc = dfs_file_checksum(filename, &crc_table);

                if(client_crc == server_crc){
                    return grpc::Status(grpc::StatusCode::ALREADY_EXISTS, "No change to this file");
                }

            }

            std::cout << "Started receiving file: " << filename << std::endl;

            // Process the first chunk of data

            out_file.open(filename, std::ios::binary);

            if (!out_file.is_open()) {

                /*release write access*/

                response->set_status("FAILED");
                ///////////////////////////////

                
                return grpc::Status(grpc::StatusCode::INTERNAL, "Unable to open file.");
            }

            out_file.write(request.data().c_str(), request.data().size());
            bytes_received += request.data().size();
        }

        if (context->IsCancelled()) {

            return Status(StatusCode::DEADLINE_EXCEEDED, "Deadline exceeded or Client cancelled, abandoning.");
        }
            
        // Continue reading and writing data for subsequent chunks
        while (reader->Read(&request)) {
            std::cout << "writing" << std::endl;
            out_file.write(request.data().c_str(), request.data().size());
            bytes_received += request.data().size();
        }

        out_file.close();

        response->set_status("Success");

        return Status::OK;
    
    }

    Status Fetch(ServerContext* context, const dfs_service::FetchRequest* request, ServerWriter<dfs_service::FetchResponse>* writer) override{
        
       if (context->IsCancelled()) {
            return Status(StatusCode::DEADLINE_EXCEEDED, "Deadline exceeded or Client cancelled, abandoning.");
        }

        std::string WrappedPath = WrapPath(request->filename());

        std::uint32_t server_crc = dfs_file_checksum(WrappedPath, &crc_table);
         std::uint32_t client_crc = request->crc();

        std::cout << "server crc: " << server_crc <<std::endl;
        std::cout << "client crc: " << client_crc <<std::endl;

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

        }else if(client_crc == server_crc){
             std::cout << "Client_CRC = Server_CRC"<<std::endl;
            dfs_service::FetchResponse response;
            response.set_samecrc(1);

            if (!writer->Write(response)){

                std::cout << "RPC FAIL "<<std::endl;
                return grpc::Status(grpc::StatusCode::INTERNAL, "RPC FAIL");
            }
            
            return grpc::Status(grpc::StatusCode::ALREADY_EXISTS, "Client_CRC = Server_CRC");
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

    Status Stat(ServerContext* context, const dfs_service::StatusRequest* request, dfs_service::FileStatus* status) override {
       struct stat fileStat; 

       std::string filename = WrapPath(request->filename());

        if (context->IsCancelled()) {     

            return Status(StatusCode::DEADLINE_EXCEEDED, "Deadline exceeded or Client cancelled, abandoning.");
        }

        if (stat(filename.c_str(), &fileStat) != 0){ 

            std::cout << "file " << request->filename() << " not found.\n"<<std::endl;
            return grpc::Status(grpc::StatusCode::NOT_FOUND, "File not found.");
        }

        //https://www.geeksforgeeks.org/how-to-delete-a-file-in-cpp/
        //remove file

        status->set_filename(request->filename()); 

        int64_t mtime = static_cast<int64_t>(fileStat.st_mtime);
        std::string mtimeBytes(reinterpret_cast<const char*>(&mtime), sizeof(mtime));

        status->set_modifiedtime(mtimeBytes);
        std::cout << "File: " << request->filename() << ", Modified: " << std::ctime(&fileStat.st_mtime);

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
// structure. You may add additional methods or change these slightly
// to add additional startup/shutdown routines inside, but be aware that
// the basic structure should stay the same as the testing environment
// will be expected this structure.
//
/**
 * The main server node constructor
 *
 * @param mount_path
 */
DFSServerNode::DFSServerNode(const std::string &server_address,
        const std::string &mount_path,
        int num_async_threads,
        std::function<void()> callback) :
        server_address(server_address),
        mount_path(mount_path),
        num_async_threads(num_async_threads),
        grader_callback(callback) {}
/**
 * Server shutdown
 */
DFSServerNode::~DFSServerNode() noexcept {
    dfs_log(LL_SYSINFO) << "DFSServerNode shutting down";
}

/**
 * Start the DFSServerNode server
 */
void DFSServerNode::Start() {
    DFSServiceImpl service(this->mount_path, this->server_address, this->num_async_threads);


    dfs_log(LL_SYSINFO) << "DFSServerNode server listening on " << this->server_address;
    service.Run();
}

//
// STUDENT INSTRUCTION:
//
// Add your additional definitions here
//
