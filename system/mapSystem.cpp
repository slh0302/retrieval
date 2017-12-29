
//
// Created by slh on 17-12-27.
//

#include <iostream>
#include "boost/timer.hpp"
#include "boost/thread.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "boost/algorithm/string.hpp"
#include <feature.h>
#include <gpu/StandardGpuResources.h>
#include <gpu/GpuIndexIVFPQ.h>
#include <gpu/GpuAutoTune.h>
#include <index_io.h>
#include <sstream>
#include <vector>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include "binary.h"


// task list

// time func
double elapsed ()
{
    struct timeval tv;
    gettimeofday (&tv, NULL);
    return  tv.tv_sec + tv.tv_usec * 1e-6;
}

// map vehicle
void LoadVehicleSpace();

// map person
void LoadPersonSpace();

// client person
void ClientPersonThread(int client_sockfd, char* remote_addr,
                        feature_index::FeatureIndex feature_index,
                        faiss::gpu::GpuIndexIVFPQ* index_person, caffe::Net<float>* pnet);
// client thread
void ClientBinaryThread(int client_sockfd, char* remote_addr, feature_index::FeatureIndex feature_index,
                        void* p, caffe::Net<float>* bnet);

// info string
#define DATA_COUNT_PERSON 8046
#define DATA_BINARY 371
#define FEATURE_GPU 0
#define FAISS_GPU 1
#define FAISS_PERSON_GPU 1

std::string INPUT_DIR = "/home/videouser/tmp/search/";
std::string ROOT_RESULT = "/home/videouser/tmp/result/ori/";
std::string ROOT_FILE_RESULT = "/home/videouser/tmp/result/run/";
std::string ROOT_CAR_DIR = "/home/videouser/tmp/car/temp3/";
std::string ROOT_PERSON_DIR = "/home/videouser/code/retrieval/data/out826/";
std::string ROOT_OTHER_FILE = "../map/";
// Person Model File
std::string person_proto_file = ROOT_OTHER_FILE + "person.prototxt";
std::string person_proto_weight = ROOT_OTHER_FILE + "person.caffemodel";
// Binary Model File
std::string binary_proto_file = ROOT_OTHER_FILE + "binary.prototxt";
std::string binary_proto_weight = ROOT_OTHER_FILE + "binary.caffemodel";


struct Info_String
{
    char info[100];
};
Info_String* info;
Info_String* info_person;

// vehicle space
std::string spaceVehicle[100];
std::string spaceOfNameVehicle[100];
// person space
std::string spacePerson[100];
std::string spaceOfNamePerson[100];

int main(int argc, char *argv[])
{
    google::InitGoogleLogging(argv[0]);

    // Load Perosn Info
    info_person = new Info_String[DATA_COUNT_PERSON];
    FILE* fin = fopen( (ROOT_OTHER_FILE + "data_person_map_8046_info").c_str(),"rb");
    fread(info_person, sizeof(Info_String),DATA_COUNT_PERSON,fin);
    fclose(fin);
    std::cout<<"Person Info File Init Done"<<std::endl;

    // Init Feature Gpu
    feature_index::FeatureIndex fea_index;
    feature_index::FeatureIndex fea_index_person;

    // Init person Faiss GPU Index
    std::string personFileName = ROOT_OTHER_FILE +"index_person_map_32bit.faissindex";
    faiss::gpu::StandardGpuResources resources_person;
    faiss::gpu::GpuClonerOptions* options = new faiss::gpu::GpuClonerOptions();
    options->usePrecomputed = false;
    faiss::Index* cpu_index_person = faiss::read_index(personFileName.c_str(), false);
    faiss::gpu::GpuIndexIVFPQ* index_person = dynamic_cast<faiss::gpu::GpuIndexIVFPQ*>(
            faiss::gpu::index_cpu_to_gpu(&resources_person,FAISS_PERSON_GPU,cpu_index_person, options));
    std::cout<<"Person Faiss Init Done"<<std::endl;

    // read data
    float* dataPerson = new float[DATA_COUNT_PERSON*1024];
    //   std::string filename = argv[1];
    // file read data
    FILE* f = fopen ( (ROOT_OTHER_FILE + "data_person_map_8046").c_str(),"rb");
    if(f == NULL){
        std::cout<<"File "<<argv[1]<<" is not right"<<std::endl;
        return 0;
    }
    fread(dataPerson,sizeof(float), DATA_COUNT_PERSON*1024, f);
    index_person->add( DATA_COUNT_PERSON ,dataPerson);


    // Init Caffe Person Model Net
    caffe::Net<float>* pnet = fea_index.InitNet(person_proto_file, person_proto_weight);
    std::cout<<"Caffe person Net Init Done"<<std::endl;


    // Init Binary Index
    void * p = FeatureBinary::CreateIndex(0);

    // Load Binary Table
    std::string table_filename= ROOT_OTHER_FILE + "table.index";
    if(!std::fstream(table_filename.c_str())) {
        std::cout << "Table File Wrong" << std::endl;
        return 1;
    }
    FeatureBinary::CreateTable(table_filename.c_str(), 16);

    // Load Binary Index

    // TODO:: Index File Name Change
    std::string IndexFileName(ROOT_OTHER_FILE + "data_binary_map_371");
    std::string IndexInfoName = IndexFileName + "_info";
    FeatureBinary::LoadIndex(p, IndexFileName.c_str(), IndexInfoName.c_str(), DATA_BINARY);


    //std::cout<<"data Set "<<((FeatureBinary::feature*)p)->getDataSet()[1].data[1]<<std::endl;
    // Load Binary Caffe Model
    caffe::Net<float>* bnet = fea_index.InitNet(binary_proto_file, binary_proto_weight);
    std::cout<<"Binary Caffe Net Init Done"<<std::endl;


    // Load Map Vehicle
    LoadPersonSpace();
    LoadVehicleSpace();

    // server status
    int server_sockfd;//服务器端套接字
    int client_sockfd;//客户端套接字
    int len;
    struct sockaddr_in my_addr;   //服务器网络地址结构体
    struct sockaddr_in remote_addr; //客户端网络地址结构体
    socklen_t sin_size;
    char buf[BUFSIZ];  //数据传送的缓冲区
    memset(&my_addr,0,sizeof(my_addr)); //数据初始化--清零
    my_addr.sin_family=AF_INET; //设置为IP通信
    my_addr.sin_addr.s_addr=INADDR_ANY;//服务器IP地址--允许连接到所有本地地址上
    my_addr.sin_port=htons(19000); //服务器端口号

    /*创建服务器端套接字--IPv4协议，面向连接通信，TCP协议*/
    if((server_sockfd=socket(PF_INET,SOCK_STREAM,0))<0)
    {
        perror("socket");
        return 1;
    }

    /*将套接字绑定到服务器的网络地址上*/
    if (bind(server_sockfd,(struct sockaddr *)&my_addr,sizeof(struct sockaddr))<0)
    {
        perror("bind");
        return 1;
    }

    /*监听连接请求--监听队列长度为5*/
    listen(server_sockfd,10);

    sin_size=sizeof(struct sockaddr_in);

    std::cout<<"Server Begin"<<std::endl;

    while(1){

        if((client_sockfd=accept(server_sockfd,(struct sockaddr *)&remote_addr,&sin_size))<0)
        {
            perror("accept");
            break;
        }
        printf("accept client %s\n",inet_ntoa(remote_addr.sin_addr));

        //len= send(client_sockfd,"Welcome to my server\n",21,0);//发送欢迎信息
        /*等待客户端连接请求到达*/

        // double info
        int typeNum = -1;
        if(len=recv(client_sockfd,buf,BUFSIZ,0)>0){
            buf[len]='\0';
            typeNum = atoi(buf);
            send(client_sockfd,"Welcome\n",7,0);
        }
        switch (typeNum){
            case 1:
            {
                boost::thread thread_2(boost::bind(&ClientPersonThread, client_sockfd,
                inet_ntoa(remote_addr.sin_addr), fea_index, index_person, pnet));
                break;
            }
                // binary situation


            case 2:
            {
                boost::thread thread_3(boost::bind(&ClientBinaryThread, client_sockfd,
                inet_ntoa(remote_addr.sin_addr),fea_index, p, bnet));
                break;
            }
            case -1:
                break;
        }
        // thread
        // threads.join();
    }
    close(server_sockfd);
    return 0;
}



void ClientPersonThread(int client_sockfd, char* remote_addr,
                        feature_index::FeatureIndex feature_index,
                        faiss::gpu::GpuIndexIVFPQ* index_person,
                        caffe::Net<float>* pnet)
{
    int len = 0;
    char buf[BUFSIZ];
    int numPicInOnePlacePerson[100] = {0};
    std::string locationStrPerson[20];
    std::vector<std::string> run_param;
    if((len=recv(client_sockfd,buf,BUFSIZ,0))>0)
    {
        buf[len]='\0';
        printf("%s\n",buf);

        // handle buf
        std::string temp = buf;
        boost::split(run_param, temp, boost::is_any_of(" ,!"), boost::token_compress_on);
        // run time param
        std::string file_name = run_param[0];
        int count = atoi(run_param[1].c_str());
        int Limit = atoi(run_param[2].c_str());

        // read data
        std::vector<cv::Mat> pic_list;
        std::vector<int> label;
        cv::Mat cv_origin = cv::imread(INPUT_DIR + file_name,1);
        cv::Mat cv_img ;
        cv::resize(cv_origin,cv_img, cv::Size(224,224));
        pic_list.push_back(cv_img);
        label.push_back(0);

        // Extract feature
        feature_index.InitGpu("GPU", FEATURE_GPU);
        std::cout<<"GPU Init Done"<<std::endl;
        float *data ;
        double t0 = elapsed();
        data = feature_index.MemoryPictureFeatureExtraction(count, pnet, "loss3/feat_normalize", pic_list, label);
        // std::cout<<"done data"<<std::endl;

        // Retrival k-NN
        int k = 20;
        int nq = 1;
        // result return
        std::vector<faiss::Index::idx_t> nns (k * nq);
        std::vector<float>               dis (k * nq);
        index_person->setNumProbes(Limit);

        index_person->search(nq, data, k, dis.data(), nns.data());
        double t1 = elapsed();

        // handle result
        char send_buf[BUFSIZ];
        std::string result_path = "";
        // output the result
        std::vector<std::string> file_name_list;
        std::string root_dir;
        root_dir = ROOT_PERSON_DIR;
        std::map<int, int*> spaceMap ;
        for (int i = 0; i < nq; i++) {
            for (int j = 0; j < k; j++) {
                std::string tempInfo;
                tempInfo = info_person[nns[j + i * k]].info;
                if(tempInfo.length() < 5){
                    continue;
                }
                boost::split(file_name_list, tempInfo, boost::is_any_of(" ,!"), boost::token_compress_on);
                cv::Mat im = cv::imread(root_dir + file_name_list[0]);
                int y = atoi(file_name_list[1].c_str());
                int x = atoi(file_name_list[2].c_str());
                int width = atoi(file_name_list[3].c_str());
                int height = atoi(file_name_list[4].c_str());
                int numSpace = atoi(file_name_list[5].c_str());

                rectangle(im,cvPoint(x,y),cvPoint(x+width, y+height),cv::Scalar(0,0,255),3,1,0);

                //
                int* listPic;
                if(numPicInOnePlacePerson[numSpace] == 0){
                    // ther smaller j, the top ranker pic
                    listPic = new int[20];
                    listPic[0] = j;
                    spaceMap.insert(std::pair<int, int*>(numSpace, listPic));
                    numPicInOnePlacePerson[numSpace] ++ ;
                }else{
                    listPic = spaceMap[numSpace];
                    listPic[numPicInOnePlacePerson[numSpace]] = j;
                    numPicInOnePlacePerson[numSpace] ++;
                }


                //out im
                IplImage qImg;
                qImg = IplImage(im); // cv::Mat -> IplImage
                char stemp[200];
                char stemp1[200];
                int index_slash = file_name.find_last_of('/');
                int index_dot = file_name.find_last_of('.');
                file_name = file_name.substr(index_slash+1,index_dot- index_slash-1);
                sprintf(stemp1, "%s_%d.jpg",file_name.c_str(),j);
                sprintf(stemp,(ROOT_RESULT + "%s_%d.jpg").c_str(),file_name.c_str(),j);
                cvSaveImage(stemp,&qImg);
                locationStrPerson[j] = stemp1;
            }
        }

        std::map<int, int*>::iterator it;
        std::ofstream reout(ROOT_FILE_RESULT + "map.txt",std::ios::out);
        reout<<(t1 - t0)<<std::endl;
        for(it = spaceMap.begin();it != spaceMap.end(); it++){
            int numSp = it->first;
            int* listPic = it->second;
            int totalPicNum = numPicInOnePlacePerson[numSp];
            reout << spaceOfNamePerson[numSp]<< "---" << totalPicNum << "个结果" <<std::endl;
            reout << totalPicNum << std::endl;
            // point
            reout << spacePerson[numSp] <<std::endl;
            // content and url
            for(int o = 0 ;o < totalPicNum; o ++ ){
                // first is content
                reout << locationStrPerson[listPic[o]] << std::endl;
            }
        }
        reout.close();
        sprintf(send_buf, "Done Work OK\0");
        std::string te(send_buf);
        int send_len = te.length();
        if(send(client_sockfd,send_buf,send_len,0)<0)
        {
            printf("Server Ip: %s error\n",remote_addr);
        }
    }
    close(client_sockfd);
    printf("Server Ip: %s done\n",remote_addr);
}


void ClientBinaryThread(int client_sockfd, char* remote_addr, feature_index::FeatureIndex feature_index,
                        void* p, caffe::Net<float>* bnet)
{
    int len = 0;
    char buf[BUFSIZ];
    int numPicInOnePlaceVehicle[100] = {0};
    std::string locationStrVehicle[20];
    std::vector<std::string> run_param;
    FeatureBinary::feature* tempFeature = (FeatureBinary::feature*) p;
    if((len=recv(client_sockfd,buf,BUFSIZ,0))>0)
    {
        buf[len]='\0';
        printf("%s\n",buf);

        // handle buf
        std::string temp = buf;
        boost::split(run_param, temp, boost::is_any_of(" ,!"), boost::token_compress_on);
        // run time param
        std::string file_name = run_param[0];
        int count = atoi(run_param[1].c_str());
        int Limit = atoi(run_param[2].c_str());

        // read data
        std::vector<cv::Mat> pic_list;
        std::vector<int> label;
        cv::Mat cv_origin = cv::imread(INPUT_DIR + file_name,1);
        cv::Mat cv_img ;
        cv::resize(cv_origin,cv_img, cv::Size(224,224));
        pic_list.push_back(cv_img);
        label.push_back(0);

        // Extract feature
        feature_index.InitGpu("GPU", FEATURE_GPU);
        std::cout<<"GPU Init Done"<<std::endl;
        unsigned char *data = new unsigned char[ 1024 ];
        double t0 = elapsed();
        feature_index.MemoryPictureFeatureExtraction(count, data, bnet, "fc_hash/relu", pic_list, label);
        //std::cout<<"done data"<<std::endl;

        // Retrival k-NN
        int k = 20;
        int nq = 1;
        // result return
        std::vector<faiss::Index::idx_t> nns (k * nq);
        std::vector<float>               dis (k * nq);

        FeatureBinary::SortTable* sorttable = new FeatureBinary::SortTable[DATA_BINARY];
        FeatureBinary::DataSet* get_t=tempFeature->getDataSet();
        FeatureBinary::Info_String* get_info=tempFeature->getInfoSet();
        std::string res;
        int* dt = FeatureBinary::DoHandle(data);
        // bianary search
        int index_num = FeatureBinary::retrival(dt, get_t, get_info, DATA_BINARY, res, 16, Limit, sorttable);
        double t1 = elapsed();
        // handle result
        char send_buf[BUFSIZ];
        std::string result_path = "";
        // output the result
        std::vector<std::string> file_name_list;

        std::map<int, int*> spaceMap ;
        std::string root_dir = ROOT_CAR_DIR;
        int return_num= 20<index_num ? 20:index_num;
        for (int j = 0; j < return_num; j++) {
            std::string tempInfo = get_info[sorttable[j].info].info;
            //std::cout<<temp<<std::endl;
            if(tempInfo.length() < 5){
                continue;
            }
            //std::cout<<temp<<std::endl;
            boost::split(file_name_list, tempInfo, boost::is_any_of(" ,!"), boost::token_compress_on);
            cv::Mat im = cv::imread(root_dir + file_name_list[0]);
            int x = atoi(file_name_list[1].c_str());
            int y = atoi(file_name_list[2].c_str());
            int width = atoi(file_name_list[3].c_str());
            int height = atoi(file_name_list[4].c_str());
            int numSpace = atoi(file_name_list[5].c_str());
            //std::cout<<x<<" "<<y<<" "<< numSpace <<std::endl;
            rectangle(im,cvPoint(x,y),cvPoint(x+width, y+height),cv::Scalar(0,0,255),3,1,0);

            int* listPic;
            if(numPicInOnePlaceVehicle[numSpace] == 0){
                // ther smaller j, the top ranker pic
                listPic = new int[20];
                listPic[0] = j;
                spaceMap.insert(std::pair<int, int*>(numSpace, listPic));
                numPicInOnePlaceVehicle[numSpace] ++ ;
            }else{
                listPic = spaceMap[numSpace];
                listPic[numPicInOnePlaceVehicle[numSpace]] = j;
                numPicInOnePlaceVehicle[numSpace] ++;
            }

            IplImage qImg;
            qImg = IplImage(im); // cv::Mat -> IplImage
            char stemp[200];
            char stemp1[200];
            int index_slash = file_name.find_last_of('/');
            int index_dot = file_name.find_last_of('.');
            file_name = file_name.substr(index_slash+1,index_dot- index_slash-1);
            sprintf(stemp,(ROOT_RESULT + "%s_%d.jpg").c_str(),file_name.c_str(),j);
            sprintf(stemp1, "%s_%d.jpg",file_name.c_str(),j);
            cvSaveImage(stemp,&qImg);
            locationStrVehicle[j] = stemp1;
        }


        std::ofstream reout( ROOT_FILE_RESULT + "map.txt",std::ios::out);
        std::map<int, int*>::iterator it;
        reout<<(t1 - t0)<<std::endl;
        for(it = spaceMap.begin();it != spaceMap.end(); it++){
            int numSp = it->first;
            int* listPic = it->second;
            int totalPicNum = numPicInOnePlaceVehicle[numSp];
            reout << spaceOfNameVehicle[numSp]<< "---" << totalPicNum << "个结果" <<std::endl;
            reout << totalPicNum << std::endl;
            // point
            reout << spaceVehicle[numSp] <<std::endl;
            // content and url
            for(int o = 0 ;o < totalPicNum; o ++ ){
                // first is content
                reout << locationStrVehicle[listPic[o]] << std::endl;
            }
        }
        reout.close();
        sprintf(send_buf, "OK\0");
        std::string te(send_buf);
        int send_len = te.length();
        if(send(client_sockfd,send_buf,send_len,0)<0)
        {
            printf("Server Ip: %s error\n",remote_addr);
        }
    }
    close(client_sockfd);
    printf("Server Ip: %s done\n",remote_addr);

}

void LoadVehicleSpace(){
    std::ifstream in(ROOT_OTHER_FILE + "wd_small_space", std::ios::in);
    int num;
    if(!in){
        std::cout<<"Wd_space file wrong"<<std::endl;
        return;
    }
    std::string temp, spaceNum;
    while(in>>num>>spaceNum>>temp){

        spaceVehicle[num] = temp;
        spaceOfNameVehicle[num] = spaceNum;
        // std::cout<<num<<" "<<spaceOfNum[num]<<std::endl;
    }
    in.close();
}

void LoadPersonSpace(){
    std::ifstream in(ROOT_OTHER_FILE + "map.txt", std::ios::in);
    int num;
    if(!in){
        std::cout<<"Person file wrong"<<std::endl;
        return;
    }
    std::string temp, spaceNum;
    while(in>>num>>spaceNum>>temp){

        spacePerson[num] = temp;
        spaceOfNamePerson[num] = spaceNum;
        // std::cout<<num<<" "<<spaceOfNum[num]<<std::endl;
    }
    in.close();
}
