#include "data_log.h"
#include <ctype.h>

#define MAX_LINE_LENGTH 1024
#define MAX_COLUMNS 1000
#define INITIAL_CHANNEL_CAPACITY 500

#define MAX(a,b) ((a) > (b) ? (a) : (b))

void channel_add_message(Channel* channel, Message* msg) {
    if (channel->message_count >= channel->message_capacity) {
        size_t new_capacity = channel->message_capacity * 2;
        Message* new_messages = realloc(channel->messages, new_capacity * sizeof(Message));
        if (!new_messages) return;
        channel->messages = new_messages;
        channel->message_capacity = new_capacity;
    }
    channel->messages[channel->message_count++] = *msg;
}

void datalog_add_channel(DataLog* log, Channel* channel) {
    if (log->channel_count >= log->channel_capacity) {
        size_t new_capacity = log->channel_capacity * 2;
        Channel** new_channels = realloc(log->channels, new_capacity * sizeof(Channel*));
        if (!new_channels) return;
        log->channels = new_channels;
        log->channel_capacity = new_capacity;
    }
    log->channels[log->channel_count++] = channel;
}


// Helper function to check if string is numeric
int is_numeric(const char* str) {
    if (!str || !*str) return 0;  // Check for NULL or empty string
    // printf("Debug - Checking if numeric: '%s'\n", str); // Debug
    char* endptr;
    strtod(str, &endptr);
    return *endptr == '\0' || isspace((unsigned char)*endptr);
}

DataLog* datalog_create(const char* name) {
    DataLog* log = (DataLog*)malloc(sizeof(DataLog));
    if (!log) return NULL;
    
    log->name = strdup(name);
    log->channel_capacity = INITIAL_CHANNEL_CAPACITY;
    log->channel_count = 0;
    log->channels = (Channel**)malloc(sizeof(Channel*) * log->channel_capacity);
    
    return log;
}

void datalog_clear(DataLog* log) {
    for (size_t i = 0; i < log->channel_count; i++) {
        channel_destroy(log->channels[i]);
    }
    log->channel_count = 0;
}

void datalog_destroy(DataLog* log) {
    if (log) {
        datalog_clear(log);
        free(log->channels);
        free(log->name);
        free(log);
    }
}

int datalog_from_csv_log(DataLog* log, FILE* f) {
    if (!f) return -1;
    
    char line[MAX_LINE_LENGTH];
    
    // Read header line (channel names)
    if (!fgets(line, sizeof(line), f)) return -1;
    char* header = strdup(line);
    
    // Read units line
    if (!fgets(line, sizeof(line), f)) {
        free(header);
        return -1;
    }
    char* units = strdup(line);
    
    // Parse header to get channel names
    char* header_copy = strdup(header);
    char* units_copy = strdup(units);
    char* token = strtok(header_copy, ",");
    char* unit_token = strtok(units_copy, ",");
    
    // Skip first column (time)
    token = strtok(NULL, ",");
    unit_token = strtok(NULL, ",");
    
    // Create channels
    while (token && unit_token) {
        // Remove newline if present
        char* nl = strchr(token, '\n');
        if (nl) *nl = '\0';
        nl = strchr(unit_token, '\n');
        if (nl) *nl = '\0';
        
        // Trim whitespace
        trim_whitespace(token);
        trim_whitespace(unit_token);
        
        // Only create channel if name is not empty
        if (strlen(token) > 0) {
            Channel* channel = channel_create(token, unit_token, FLOAT_TYPE, 0);
            if (channel) {
                datalog_add_channel(log, channel);
            }
        }
        
        token = strtok(NULL, ",");
        unit_token = strtok(NULL, ",");
    }
    
    free(header_copy);
    free(units_copy);
    
    // Read data lines
    while (fgets(line, sizeof(line), f)) {
        char* line_copy = strdup(line);
        char* value_token = strtok(line_copy, ",");
        
        // First column is timestamp
        double timestamp = atof(value_token);
        
        // Process each channel
        size_t channel_idx = 0;
        value_token = strtok(NULL, ",");
        
        while (value_token && channel_idx < log->channel_count) {
            Channel* channel = log->channels[channel_idx];
            
            // Try to parse value as float
            if (is_numeric(value_token)) {
                double value = atof(value_token);
                
                // Update decimals
                char* decimal_point = strchr(value_token, '.');
                if (decimal_point) {
                    int decimals = strlen(decimal_point + 1);
                    channel->decimals = MAX(channel->decimals, decimals);
                }
                
                // Add message
                Message msg = {timestamp, value};
                channel_add_message(channel, &msg);
            }
            
            channel_idx++;
            value_token = strtok(NULL, ",");
        }
        
        free(line_copy);
    }
    
    free(header);
    free(units);
    
    return 0;
}

// end function


void data_log_print_channels(DataLog* log) {
    printf("Parsed %.1fs log with %zu channels:\n", datalog_duration(log), log->channel_count);
    
    for (size_t i = 0; i < log->channel_count; i++) {
        Channel* channel = log->channels[i];
        double freq = channel_avg_frequency(channel);
        printf("        Channel: %s, Units: %s, Decimals: %d, Messages: %zu, Frequency: %.2f Hz\n",
               channel->name,
               channel->units,
               channel->decimals,
               channel->message_count,
               freq);
    }
}

double channel_avg_frequency(Channel* channel) {
    if (channel->message_count < 2) return 0.0;
    
    double duration = channel->messages[channel->message_count-1].timestamp - 
                     channel->messages[0].timestamp;
    if (duration <= 0.0) return 0.0;
    
    return (channel->message_count - 1) / duration;
}


void channel_destroy(Channel* channel) {
    if (channel) {
        free(channel->name);
        free(channel->units);
        free(channel->messages);
        free(channel);
    }
}

int datalog_channel_count(DataLog* log) {
    return log->channel_count;
}

double datalog_duration(DataLog* log) {
    if (log->channel_count == 0) return 0.0;
    return datalog_end(log) - datalog_start(log);
}

void datalog_free(DataLog* log) {
    if (log) {
        datalog_destroy(log);
    }
}

int datalog_from_can_log(DataLog* log, FILE* f, const char* dbc_path) {
    // Can stuff, for now returns error
    // For now returning error
    return -1;
}


int datalog_from_accessport_log(DataLog* log, FILE* f) {
    // Implementation depends on Accessport format
    // For now returning error
    return -1;
}


// void datalog_add_channel(DataLog* log, const char* name, const char* units, int decimals) {
//     if (log->channel_count >= log->channel_capacity) {
//         log->channel_capacity *= 2;
//         log->channels = realloc(log->channels, sizeof(Channel*) * log->channel_capacity);
//     }
    
//     Channel* channel = channel_create(name, units, decimals, 1000);
//     log->channels[log->channel_count++] = channel;
// }

double datalog_start(DataLog* log) {
    if (log->channel_count == 0) return 0.0;
    
    double earliest = DBL_MAX;
    for (size_t i = 0; i < log->channel_count; i++) {
        double start = channel_start(log->channels[i]);
        if (start < earliest) earliest = start;
    }
    return earliest;
}

double datalog_end(DataLog* log) {
    if (log->channel_count == 0) return 0.0;
    
    double latest = 0.0;
    for (size_t i = 0; i < log->channel_count; i++) {
        double end = channel_end(log->channels[i]);
        if (end > latest) latest = end;
    }
    return latest;
}

//////////

// 888888888

Channel* channel_create(const char* name, const char* units, int decimals, size_t initial_size) {
    Channel* channel = (Channel*)malloc(sizeof(Channel));
    if (!channel) return NULL;
    
    channel->name = strdup(name);
    channel->units = strdup(units);
    channel->decimals = decimals;
    channel->message_count = 0;
    channel->message_capacity = initial_size;
    channel->messages = (Message*)malloc(sizeof(Message) * initial_size);
    channel->data_type = NULL;
    channel->frequency = 0.0;
    
    return channel;
}

double channel_start(Channel* channel) {
    if (!channel || channel->message_count == 0) return 0.0;
    return channel->messages[0].timestamp;
}

double channel_end(Channel* channel) {
    if (!channel || channel->message_count == 0) return 0.0;
    return channel->messages[channel->message_count - 1].timestamp;
}

// 88888888


// csv parsing:

void trim_whitespace(char* str) {
    char* end;
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return;
    end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
}


// Helper function to split CSV line
char** split_csv_line(char* line, int* count) {
    char** tokens = malloc(MAX_COLUMNS * sizeof(char*));
    *count = 0;
    char* token = strtok(line, ",");
    
    while (token != NULL && *count < MAX_COLUMNS) {
        tokens[*count] = strdup(token);
        trim_whitespace(tokens[*count]);
        (*count)++;
        token = strtok(NULL, ",");
    }
    
    return tokens;
}