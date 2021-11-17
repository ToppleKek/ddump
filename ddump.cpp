#include <iostream>
#include <curl/curl.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <regex>
#include <filesystem>
#include "thirdparty/json.hpp"

const static std::string MESSAGE_TYPES[] = {
        "DEFAULT",
        "RECIPIENT_ADD",
        "RECIPIENT_REMOVE",
        "CALL",
        "CHANNEL_NAME_CHANGE",
        "CHANNEL_ICON_CHANGE",
        "CHANNEL_PINNED_MESSAGE",
        "GUILD_MEMBER_JOIN",
        "USER_PREMIUM_GUILD_SUBSCRIPTION",
        "USER_PREMIUM_GUILD_SUBSCRIPTION_TIER_1",
        "USER_PREMIUM_GUILD_SUBSCRIPTION_TIER_2",
        "USER_PREMIUM_GUILD_SUBSCRIPTION_TIER_3",
        "CHANNEL_FOLLOW_ADD",
        "GUILD_DISCOVERY_DISQUALIFIED",
        "GUILD_DISCOVERY_REQUALIFIED",
        "GUILD_DISCOVERY_GRACE_PERIOD_INITIAL_WARNING",
        "GUILD_DISCOVERY_GRACE_PERIOD_FINAL_WARNING",
        "THREAD_CREATED",
        "REPLY",
        "APPLICATION_COMMAND",
        "THREAD_STARTER_MESSAGE",
        "GUILD_INVITE_REMINDER"
};

size_t curl_write(void *ptr, size_t size, size_t nmemb, std::string *data) {
    data->append(static_cast<char *>(ptr));
    return size * nmemb;
}

size_t curl_write_binary(void *ptr, size_t size, size_t nmemb, std::ofstream *file) {
    file->write(static_cast<char *>(ptr), nmemb);
    return size * nmemb;
}

CURLcode get(CURL *curl, const std::string &url, std::string *response) {
    response->clear();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    return curl_easy_perform(curl);
}

std::string csv_escape(const std::string &str) {
    std::stringstream ss;
    ss << '"' << std::regex_replace(str, std::regex("\""), "\"\"") << '"';
    return std::regex_replace(ss.str(), std::regex("\\n"), "<NEWLINE>");
}

bool download_file(const std::string &url, const std::string &output_file_name) {
    CURL *curl = curl_easy_init();
    std::ofstream output_file(output_file_name, std::ios_base::binary);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_binary);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &output_file);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

    curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    return output_file.good();
}

void dump_channel(const std::string &token, const std::string &channel_id, bool download_attachments) {
    const std::string BASE_CHANNEL_URL = "https://discord.com/api/v9/channels/" + channel_id;
    const std::string BASE_URL = BASE_CHANNEL_URL + "/messages?limit=100";
    const std::string AUTHORIZATION_HEADER = "authorization: " + token;
    int num_requests = 0;

    CURL *curl = curl_easy_init();
    std::string response;
    curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, AUTHORIZATION_HEADER.c_str());

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    get(curl, BASE_CHANNEL_URL, &response);

    auto channel_data = nlohmann::json::parse(response);

    if (!channel_data.contains("name")) {
        std::cerr << "err: response does not look like a channel:\n" << response << '\n';
        std::exit(1);
    }

    std::string channel_name = channel_data["name"].get<std::string>();
    std::string url = BASE_URL;

    std::cout << "dumping channel " << channel_id << " (" << channel_name << ")\n";
    const std::string ATTACHMENT_DIRECTORY = channel_name + '_' + channel_id + '_' + "attachments/";

    if (download_attachments)
        std::filesystem::create_directory(ATTACHMENT_DIRECTORY);

    get(curl, url, &response);

    auto messages = nlohmann::json::parse(response);

    if (!messages.is_array()) {
        std::cerr << "err: invalid response from discord:\n" + response + '\n';
        std::exit(1);
    }

    std::string last_message_id;
    std::vector<std::string> all_messages_text;
    std::vector<std::string> all_messages_csv;

    while (!messages.empty() && messages.is_array()) {
        last_message_id = messages[messages.size() - 1]["id"];
        for (const auto &message : messages) {
            std::string thread_text;
            std::string attachments_text;
            std::string type_text;

            try {
                if (message.contains("message_reference") && message["message_reference"].contains("message_id"))
                    thread_text = "(referencing " + message["message_reference"]["message_id"].get<std::string>() + ") ";

                if (message.contains("attachments") && !message["attachments"].is_null()) {
                    for (const auto &attachment : message["attachments"]) {
                        attachments_text.append(attachment["url"].get<std::string>() + ' ');

                        if (download_attachments)
                            download_file(attachment["url"].get<std::string>(), ATTACHMENT_DIRECTORY + message["id"].get<std::string>() + '_' + attachment["filename"].get<std::string>());
                    }
                    if (!attachments_text.empty()) {
                        attachments_text.pop_back();
                        attachments_text += '\n';
                    }
                }

                if (message["type"].get<int>() != 0)
                    type_text = "msg_type=" + MESSAGE_TYPES[message["type"].get<int>()] + ' ';

                const std::string embeds_text = message.contains("embeds") ? csv_escape(message["embeds"].dump()) : "";
                const std::string reactions_text = message.contains("reactions") ? csv_escape(message["reactions"].dump()) : "";
                const std::string mentions_text = message.contains("mentions") ? csv_escape(message["mentions"].dump()) : "";
                std::stringstream output_text;
                std::stringstream output_csv;

                output_text << '[' << message["timestamp"].get<std::string>() << "] " << thread_text << type_text
                            << '<' << message["author"]["username"].get<std::string>()
                            << '#' << message["author"]["discriminator"].get<std::string>() << "> "
                            << message["content"].get<std::string>() << '\n'
                            << (attachments_text.empty() ? "" : "[ATTACHMENTS]: ") << attachments_text;

                output_csv << message["id"].get<std::string>() << ','
                           << message["timestamp"].get<std::string>() << ','
                           << message["author"]["id"].get<std::string>() << ','
                           << csv_escape(message["author"]["username"].get<std::string>()) << ','
                           << message["author"]["discriminator"].get<std::string>() << ','
                           << csv_escape(message["content"].get<std::string>()) << ','
                           << csv_escape(attachments_text) << ','
                           << embeds_text << ','
                           << reactions_text << ','
                           << message["pinned"].get<bool>() << ','
                           << message["type"].get<int>() << ','
                           << (thread_text.empty() ? "" : thread_text) << ','
                           << mentions_text << '\n';

                all_messages_text.push_back(output_text.str());
                all_messages_csv.push_back(output_csv.str());
            } catch (std::exception &e) {
                std::cerr << "err: " << e.what() << '\n';
            }
        }

        url = BASE_URL + "&before=";
        url.append(last_message_id);

do_request:
        get(curl, url, &response);
        messages = nlohmann::json::parse(response);

        if (!messages.is_array()) {
            if (messages.contains("retry_after")) {
                // We are being rate limited
                int wait = static_cast<int>(messages["retry_after"].get<float>() * 1000000);
                std::cerr << "\nwarn: we are being rate limited. retrying in " << wait << '\n';
                usleep(wait);
                goto do_request;
            } else {
                std::cerr << "err: invalid response from discord:\n" + response << '\n';
                std::exit(1);
            }
        } else if (!messages.empty()) {
            std::cout << "\rmessage timestamp: " << messages[0]["timestamp"] << " Messages downloaded: "
                      << all_messages_text.size();
        }

        // Avoid getting rate limited
        if (++num_requests >= 20) {
            num_requests = 0;
            usleep(1700000);
        }
    }

    std::ofstream text_file(channel_name + '_' + channel_id + "_DUMP.txt", std::ios_base::out);
    std::ofstream csv_file(channel_name + '_' + channel_id + "_DUMP.csv", std::ios_base::out);

    text_file << '#' << channel_name << " - " << channel_id << '\n'
              << (!channel_data["topic"].is_null() ? channel_data["topic"].get<std::string>() : "") << "\n\n";

    csv_file << "MessageID,Timestamp,AuthorID,AuthorUsername,AuthorDiscriminator,Content,Attachments,Embeds,Reactions,Pinned,Type,MessageReference,Mentions\n";

    std::reverse(all_messages_text.begin(), all_messages_text.end());
    std::reverse(all_messages_csv.begin(), all_messages_csv.end());

    for (int i = 0; i < all_messages_text.size(); ++i) {
        text_file << all_messages_text[i];
        csv_file << all_messages_csv[i];
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    std::cout << "\ndone. dumped " << all_messages_text.size() << " messages.\n";
}

void dump_guild(const std::string &token, const std::string &guild_id, bool download_attachments) {
    const std::string AUTHORIZATION_HEADER = "authorization: " + token;
    const std::string BASE_GUILD_URL = "https://discord.com/api/v9/guilds/" + guild_id;
    CURL *curl = curl_easy_init();
    std::string response;
    curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, AUTHORIZATION_HEADER.c_str());

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    get(curl, BASE_GUILD_URL, &response);
    auto guild_data = nlohmann::json::parse(response);

    try {
        auto guild_name = guild_data["name"].get<std::string>();
        auto guild_icon_url = "https://cdn.discordapp.com/icons/" + guild_id + '/' + guild_data["icon"].get<std::string>() + ".png";
        std::stringstream emoji_text;
        std::stringstream role_text;

        std::filesystem::create_directory(guild_id + "_DUMP");
        std::filesystem::current_path(guild_id + "_DUMP");

        for (const auto &emoji : guild_data["emojis"]) {
            emoji_text << "Name: " << emoji["name"].get<std::string>()
                       << " URL: " << "https://cdn.discordapp.com/emojis/"
                       << emoji["id"].get<std::string>() << (emoji["animated"].get<bool>() ? ".gif" : ".png") << '\n';
        }

        for (const auto &role : guild_data["roles"]) {
            role_text << "Name: " << role["name"].get<std::string>()
                      << " ID: " << role["id"].get<std::string>()
                      << " Permissions: " << role["permissions"].get<std::string>()
                      << " Colour: " << role["color"].get<int>() << '\n';
        }

        std::ofstream guild_text_file("GUILD_" + guild_id + "_DUMP.txt");

        guild_text_file << "GUILD " << guild_id << " (" << guild_name << ")\n"
                        << "Icon URL: " << guild_icon_url << '\n' << "Owner: "
                        << guild_data["owner_id"].get<std::string>() << '\n'
                        << "Creation Date: "
                        << "Emoji:\n" << emoji_text.str() << "Roles:\n" << role_text.str();

        get(curl, BASE_GUILD_URL + "/channels", &response);
        auto channels = nlohmann::json::parse(response);

        for (const auto &channel : channels) {
            if (channel["type"].get<int>() == 0)
                dump_channel(token, channel["id"].get<std::string>(), download_attachments);
        }
    } catch (std::exception &e) {
        std::cerr << "err: failed to dump guild:\n" << response << '\n' << "err: " << e.what() << '\n';
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
}

void print_help() {
    std::cout << "Usage: ddump [OPTIONS]...\n-t [TOKEN] - Use [TOKEN] for authentication "
                 "(ddump also reads from the DDUMP_TOKEN environment variable, this option overrides that)\n"
                 "-c [CHANNEL_ID] - Dump the channel with the id [CHANNEL_ID]\n"
                 "-g [GUILD_ID] - Dump the guild with the id [GUILD_ID] and all its channels "
                 "(this overrides any provided channel passed with -c)\n"
                 "-d - Download all attachments\n"
                 "-h - Show this help\n";
}

int main(int argc, char **argv) {
    char *token_env_var = getenv("DDUMP_TOKEN");
    std::string token;

    if (token_env_var)
        token = token_env_var;

    std::string channel_id;
    std::string guild_id;
    bool download_attachments = false;
    int c;

    while ((c = getopt(argc, argv, "t:c:g:dh")) != -1) {
        switch (c) {
            case 't':
                token = optarg;
                break;
            case 'c':
                channel_id = optarg;
                break;
            case 'g':
                guild_id = optarg;
                break;
            case 'd':
                download_attachments = true;
                break;
            case 'h':
                print_help();
                return 0;
            default:
                print_help();
                return 1;
        }
    }

    if (token.empty()) {
        std::cerr << "No token provided\n";
        print_help();
        return 1;
    }

    if (channel_id.empty() && guild_id.empty()) {
        std::cerr << "No channel id provided\n";
        print_help();
        return 1;
    }

    if (!guild_id.empty()) {
        dump_guild(token, guild_id, download_attachments);
        return 0;
    }

    dump_channel(token, channel_id, download_attachments);
    return 0;
}
