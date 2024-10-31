#include <iostream>
#include <boost/asio.hpp>
#include <thread>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>

using boost::asio::ip::tcp;
using boost::asio::ip::udp;

void handle_telemetry_data(tcp::socket socket)
{
    try
    {
        boost::asio::streambuf buffer;
        boost::system::error_code error;

        while (true)
        {
            // Read data until a newline character is encountered
            size_t len = boost::asio::read_until(socket, buffer, '\n', error);

            if (error == boost::asio::error::eof)
            {
                std::cout << "Client disconnected." << std::endl;
                break; // Connection closed cleanly by peer.
            }
            else if (error)
            {
                throw boost::system::system_error(error); // Handle other errors
            }

            // Convert buffer data to string
            std::istream is(&buffer);
            std::string data;
            std::getline(is, data);

            std::cout << "Received telemetry: " << data << std::endl;
        }
    }
    catch (std::exception &e)
    {
        std::cerr << "Exception in telemetry handler: " << e.what() << std::endl;
    }
}

void start_telemetry_server(unsigned short port)
{
    try
    {
        boost::asio::io_context io_context;
        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), port));
        std::cout << "Telemetry server listening on port " << port << std::endl;

        while (true)
        {
            tcp::socket socket(io_context);
            acceptor.accept(socket);
            std::cout << "New telemetry client connected!" << std::endl;
            std::thread(handle_telemetry_data, std::move(socket)).detach();
        }
    }
    catch (std::exception &e)
    {
        std::cerr << "Exception in telemetry server: " << e.what() << std::endl;
    }
}

// Function to send commands to a specific drone
void send_commands(boost::asio::io_context &io_context, const std::string &drone_ip, unsigned short port, const std::string &command)
{
    try
    {
        boost::asio::ip::udp::socket socket(io_context);
        boost::asio::ip::udp::endpoint endpoint(boost::asio::ip::make_address(drone_ip), port);

        socket.open(boost::asio::ip::udp::v4());
        socket.send_to(boost::asio::buffer(command), endpoint);
        socket.close();
        std::cout << "Sent command: " << command << " to " << drone_ip << " on port " << port << std::endl;
    }
    catch (std::exception &e)
    {
        std::cerr << "Error sending command: " << e.what() << std::endl;
    }
}

void manual_command_input(boost::asio::io_context &io_context, const std::string &drone_ip1, unsigned short port1, const std::string &drone_ip2, unsigned short port2)
{
    while (true)
    {
        std::string input;
        std::cout << "Enter command (e.g., '1 move back' or '2 move left'): ";
        std::getline(std::cin, input);

        // Parse the command input
        int drone_id;
        std::string command;
        std::istringstream iss(input);
        iss >> drone_id;
        std::getline(iss, command);
        command = command.substr(1); // Remove leading space

        if (drone_id == 1)
        {
            send_commands(io_context, drone_ip1, port1, command);
        }
        else if (drone_id == 2)
        {
            send_commands(io_context, drone_ip2, port2, command);
        }
        else
        {
            std::cout << "Invalid drone ID. Use 1 or 2." << std::endl;
        }
    }
}

// Function to handle incoming file transfer from a drone
void handle_file_transfer(tcp::socket socket, const std::string &filename)
{
    try
    {
        std::ofstream outfile(filename, std::ios::binary | std::ios::trunc);
        if (!outfile)
        {
            std::cerr << "Failed to open file: " << filename << std::endl;
            return;
        }

        char data[1024];
        boost::system::error_code error;

        while (true)
        {
            size_t len = socket.read_some(boost::asio::buffer(data), error);

            if (error == boost::asio::error::eof)
                break; // Connection closed cleanly by peer
            else if (error)
                throw boost::system::system_error(error); // Handle other errors

            outfile.write(data, len);
        }

        std::cout << "File transfer completed: " << filename << std::endl;
    }
    catch (std::exception &e)
    {
        std::cerr << "Exception in file transfer handler: " << e.what() << std::endl;
    }
}

// Function to start a file transfer server for each drone
void start_file_transfer_server(unsigned short port, const std::string &filename)
{
    try
    {
        boost::asio::io_context io_context;
        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), port));
        std::cout << "File transfer server listening on port " << port << std::endl;

        while (true)
        {
            tcp::socket socket(io_context);
            acceptor.accept(socket);
            std::cout << "New file transfer client connected!" << std::endl;
            std::thread(handle_file_transfer, std::move(socket), filename).detach();
        }
    }
    catch (std::exception &e)
    {
        std::cerr << "Exception in file transfer server: " << e.what() << std::endl;
    }
}

int main()
{
    unsigned short command_port_1 = 9000;       // Port to send commands to drone 1
    unsigned short command_port_2 = 9002;       // Port to send commands to drone 2
    unsigned short telemetry_port = 9001;       // Port to receive telemetry data
    unsigned short file_transfer_port_1 = 9003; // File transfer port for drone 1
    unsigned short file_transfer_port_2 = 9004; // File transfer port for drone 2

    // Starting the telemetry server in a separate thread
    std::thread telemetry_thread(start_telemetry_server, telemetry_port);

    // Setting up command sending for each drone in separate threads
    boost::asio::io_context io_context;
    std::string drone_ip1 = "127.0.0.1"; // Replace with actual drone IP
    std::string drone_ip2 = "127.0.0.1"; // Replace with second drone IP if different

    // Start manual command input thread for sending commands to drones
    std::thread command_input_thread(manual_command_input, std::ref(io_context), drone_ip1, command_port_1, drone_ip2, command_port_2);

    // Start file transfer servers for each drone
    std::thread file_transfer_thread_1(start_file_transfer_server, file_transfer_port_1, "drone1_file.txt");
    std::thread file_transfer_thread_2(start_file_transfer_server, file_transfer_port_2, "drone2_file.txt");

    // Running the io_context to handle asynchronous operations
    std::thread io_context_thread([&io_context]()
                                  { io_context.run(); });

    // Wait for all threads to complete
    telemetry_thread.join();
    command_input_thread.join();
    file_transfer_thread_1.join();
    file_transfer_thread_2.join();
    io_context_thread.join();

    return 0;
}