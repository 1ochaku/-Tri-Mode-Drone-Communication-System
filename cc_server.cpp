#include <iostream>
#include <boost/asio.hpp>
#include <thread>
#include <string>
#include <fstream>
#include <atomic>

using boost::asio::ip::tcp;
using boost::asio::ip::udp;

// XOR Encryption/Decryption Function
std::string xor_cipher(const std::string &data, char key)
{
    std::string result = data;
    for (auto &c : result)
        c ^= key;
    return result;
}

// Receive Telemetry Data (TCP) from Drone
void receive_telemetry_data(boost::asio::io_context &io_context, unsigned short port, char key, std::atomic<bool> &telemetry_received)
{
    try
    {
        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), port));
        std::cout << "Telemetry Data Server started on port " << port << std::endl;

        tcp::socket socket(io_context);
        acceptor.accept(socket);

        // Notify about drone connection
        std::cout << "A drone has connected!" << std::endl;

        try
        {
            while (true)
            {
                boost::asio::streambuf buffer;
                boost::asio::read_until(socket, buffer, "\n");
                std::istream input_stream(&buffer);
                std::string data;
                std::getline(input_stream, data);

                std::string decrypted_data = xor_cipher(data, key);
                std::cout << "Received telemetry data: " << decrypted_data << std::endl;

                telemetry_received.store(true); // Set flag to indicate telemetry data was received
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "Exception in telemetry session: " << e.what() << std::endl;
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Exception in receive_telemetry_data: " << e.what() << std::endl;
    }
}

// Send Control Commands (UDP) from Server to Drone
void send_control_commands(boost::asio::io_context &io_context, const std::string &drone_ip, unsigned short port, char key, std::atomic<bool> &telemetry_received)
{
    try
    {
        udp::socket socket(io_context);
        udp::endpoint drone_endpoint(boost::asio::ip::make_address(drone_ip), port);

        socket.open(udp::v4()); // Open the UDP socket

        // Wait until telemetry data is received
        while (!telemetry_received.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Check periodically
        }

        std::cout << "Telemetry data received. Control Command Sender started. (NOTE: move front/back/left/right commands only ENABLED)" << std::endl;

        while (true)
        {
            std::string command;
            std::getline(std::cin, command);

            // Validate command
            if (command != "move front" && command != "move back" && command != "move left" && command != "move right")
            {
                std::cout << "Invalid command. Please enter one of the following: move front, move back, move left, move right." << std::endl;
                continue;
            }

            // Encrypt and send the command
            std::string encrypted_command = xor_cipher(command, key) + "\n"; // Add newline character
            boost::system::error_code error;
            socket.send_to(boost::asio::buffer(encrypted_command), drone_endpoint, 0, error);

            if (error)
            {
                std::cerr << "Error sending command: " << error.message() << std::endl;
                continue;
            }

            std::cout << "Sent command: " << command << " to drone at " << drone_ip << std::endl;
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Exception in send_control_commands: " << e.what() << std::endl;
    }
}

// Receive Large File Transfer (TCP) from Drone
void receive_file_transfer(boost::asio::io_context &io_context, unsigned short port, const std::string &output_file_path, std::atomic<bool> &file_received)
{
    try
    {
        tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), port));
        std::cout << "File Transfer Server started on port " << port << std::endl;

        while (true)
        {
            tcp::socket socket(io_context);
            acceptor.accept(socket);

            std::cout << "A drone has connected for file transfer!" << std::endl;

            try
            {
                std::ofstream output_file(output_file_path, std::ios::binary | std::ios::trunc); // Truncate file each time
                if (!output_file)
                {
                    std::cerr << "Failed to open output file: " << output_file_path << std::endl;
                    return;
                }

                boost::asio::streambuf buffer;
                std::cout << "Receiving file data..." << std::endl;

                // Receive file data
                while (true)
                {
                    boost::system::error_code error;
                    size_t bytes_received = boost::asio::read(socket, buffer, boost::asio::transfer_at_least(1), error);

                    if (error && error != boost::asio::error::eof)
                    {
                        std::cerr << "Error receiving file data: " << error.message() << std::endl;
                        break;
                    }

                    if (bytes_received == 0)
                        break; // End of file

                    std::ostream output_stream(&buffer);
                    output_file.write(boost::asio::buffer_cast<const char *>(buffer.data()), buffer.size());
                    buffer.consume(buffer.size());
                }

                output_file.close();
                std::cout << "File transfer completed. Saved to " << output_file_path << std::endl;
                file_received.store(true); // Set flag to indicate file was received

                // Reset the file_received flag for future use
                file_received.store(false);
            }
            catch (const std::exception &e)
            {
                std::cerr << "Exception in file transfer session: " << e.what() << std::endl;
            }
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Exception in receive_file_transfer: " << e.what() << std::endl;
    }
}

int main()
{
    char key = 0x42; // XOR cipher key
    unsigned short control_port = 9000;
    unsigned short telemetry_port = 9001;
    unsigned short file_port = 9002; // Port for file transfer

    boost::asio::io_context io_context;
    std::atomic<bool> telemetry_received(false); // Flag to ensure telemetry is received first
    std::atomic<bool> file_received(false);      // Flag to indicate file was received

    // Start threads for receiving telemetry data and file transfer
    std::thread telemetry_thread(receive_telemetry_data, std::ref(io_context), telemetry_port, key, std::ref(telemetry_received));
    std::thread file_thread(receive_file_transfer, std::ref(io_context), file_port, "received_file.bin", std::ref(file_received));

    // Wait for telemetry to be received before sending control commands
    std::thread control_thread(send_control_commands, std::ref(io_context), "127.0.0.1", control_port, key, std::ref(telemetry_received));

    // Join threads to the main thread
    telemetry_thread.join();
    file_thread.join();
    control_thread.join();

    std::cout << "Exiting program." << std::endl;
    return 0;
}