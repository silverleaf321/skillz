#include "data_log.h"
#include <ctype.h>

#define MAX_LINE_LENGTH 1024
#define MAX_COLUMNS 1000
#define INITIAL_CHANNEL_CAPACITY 500

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
    char* header = NULL;
    char* units = NULL;
    
    // Read header and units lines
    if (fgets(line, MAX_LINE_LENGTH, f)) {
        header = strdup(line);
    }
    if (fgets(line, MAX_LINE_LENGTH, f)) {
        units = strdup(line);
    }

    if (!header || !units) {
        free(header);
        free(units);
        return -1;
    }

    // Parse header and units
    char** headers = malloc(MAX_COLUMNS * sizeof(char*));
    char** unit_tokens = malloc(MAX_COLUMNS * sizeof(char*));
    int column_count = 0;
    
    // Split header line
    char* token = strtok(header, ",");
    while (token && column_count < MAX_COLUMNS) {
        headers[column_count] = strdup(token);
        trim_whitespace(headers[column_count]);
        column_count++;
        token = strtok(NULL, ",");
    }
    
    // Split units line
    int unit_count = 0;
    token = strtok(units, ",");
    while (token && unit_count < column_count) {
        unit_tokens[unit_count] = strdup(token);
        trim_whitespace(unit_tokens[unit_count]);
        unit_count++;
        token = strtok(NULL, ",");
    }

    // Create channels (skip first column which is time)
    for (int i = 1; i < column_count && i < unit_count; i++) {
        if (headers[i] && unit_tokens[i]) {
            Channel* channel = channel_create(headers[i], unit_tokens[i], 0, 1000);
            if (channel) {
                log->channels[log->channel_count++] = channel;
            }
        }
    }

    // Count number of data lines to pre-allocate message arrays
    long pos = ftell(f);
    size_t line_count = 0;
    while (fgets(line, MAX_LINE_LENGTH, f)) {
        line_count++;
    }
    fseek(f, pos, SEEK_SET);

    // Resize message arrays
    for (size_t i = 0; i < log->channel_count; i++) {
        Channel* channel = log->channels[i];
        channel->message_capacity = line_count;
        channel->messages = realloc(channel->messages, line_count * sizeof(Message));
        channel->message_count = 0;
    }

    // Parse data rows
    size_t row = 0;
    while (fgets(line, MAX_LINE_LENGTH, f)) {
        char* value_str = strtok(line, ",");
        if (!value_str || !is_numeric(value_str)) continue;
        
        double timestamp = atof(value_str);
        
        // Process each channel's value
        for (size_t i = 0; i < log->channel_count; i++) {
            value_str = strtok(NULL, ",");
            Channel* channel = log->channels[i];
            
            if (value_str && is_numeric(value_str)) {
                double value = atof(value_str);
                
                // Update decimals count
                char* decimal_point = strchr(value_str, '.');
                if (decimal_point) {
                    int decimals = strlen(decimal_point + 1);
                    channel->decimals = decimals > channel->decimals ? decimals : channel->decimals;
                }
                
                channel->messages[channel->message_count].timestamp = timestamp;
                channel->messages[channel->message_count].value = value;
                channel->message_count++;
            } else {
                // Invalid value found - remove this channel
                printf("WARNING: Found non numeric values for channel %s, removing channel\n", 
                       channel->name);
                channel_destroy(channel);
                
                // Shift remaining channels left
                for (size_t j = i; j < log->channel_count - 1; j++) {
                    log->channels[j] = log->channels[j + 1];
                }
                log->channel_count--;
                i--; // Adjust index since we removed a channel
            }
        }
        row++;
    }

    // Calculate frequency for each channel
    for (size_t i = 0; i < log->channel_count; i++) {
        Channel* channel = log->channels[i];
        if (channel->message_count >= 2) {
            double duration = channel->messages[channel->message_count-1].timestamp - 
                            channel->messages[0].timestamp;
            channel->frequency = duration > 0 ? (channel->message_count - 1) / duration : 0;
        }
    }

    // Cleanup
    for (int i = 0; i < column_count; i++) {
        free(headers[i]);
        if (i < unit_count) free(unit_tokens[i]);
    }
    free(headers);
    free(unit_tokens);
    free(header);
    free(units);
    
    return 0;
}

// end function


void data_log_print_channels(DataLog* log) {
    printf("Parsed %.1fs log with %d channels:\n", datalog_duration(log), log->channel_count);
    
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
// ********

////////////

void datalog_add_channel(DataLog* log, const char* name, const char* units, int decimals) {
    if (log->channel_count >= log->channel_capacity) {
        log->channel_capacity *= 2;
        log->channels = realloc(log->channels, sizeof(Channel*) * log->channel_capacity);
    }
    
    Channel* channel = channel_create(name, units, decimals, 1000);
    log->channels[log->channel_count++] = channel;
}

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