#include "INewMessageListener.h"

class EventQueue {
public:
    virtual std::vector<std::string> list_topics();

    // Explicitly creates a topic if it doesn't exist.
    // Produce also creates topics on demand.
    virtual bool create_topic(const std::string& topic_name);

    // Returns the offset of the produced message
    virtual uint64_t produce(const std::string& topic_name, const std::string& payload);

    // Consumes messages from a specific topic starting at start_offset
    virtual std::vector<Message> consume(const std::string& topic_name, uint64_t start_offset, uint32_t max_messages = 100);

    void add_listener(INewMessageListener* listener);
    void remove_listener(INewMessageListener* listener);

protected:
    void notify_new_message(Message new_msg); 

private:
    std::set<INewMessageListener*> listeners_;
    std::mutex listeners_mutex_;
};