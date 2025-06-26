#include <iostream>
#include <string>
#include <grpcpp/grpcpp.h>


int main(int argc, char** argv) {
  std::cout << "gPRC version: " << grpc::Version() << "\n";
  return 0;
}
