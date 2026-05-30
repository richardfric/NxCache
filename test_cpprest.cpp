#include <cpprest/http_listener.h>
#include <iostream>

int main() {
    try {
		std::cout << "Starting HTTP server test: ";
        web::http::experimental::listener::http_listener listener(U("http://127.0.0.1:8080"));
        listener.open().wait();
        std::cout << "running... ";
        listener.close().wait();
        std::cout << "success" << std::endl;
    } catch(std::exception& e) {
        std::cerr << "exception: " << e.what() << std::endl;
		return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
