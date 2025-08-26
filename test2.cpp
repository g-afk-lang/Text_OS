
#include "iostream_wrapper.h"
    



int atoi(const char* strg)
{

    // Initialize res to 0
    int res = 0;
    int i = 0;

    // Iterate through the string strg and compute res
    while (strg[i] != '\0') {
        res = res * 10 + (strg[i] - '0');
        i++;
    }
    return res;
}


void print_prog2() {


    cout << "Enter 1: ";
    char input[80]; // Assuming a maximum of 80 characters for the number
    cin >> input;  // Read the input as a string
    int a = atoi(input);
    if (a == 1) {
        cout<< "hex: " << std::hex << a << "\n";

        cout<< "dec: " << std::dec << a << "\n";
    }
}
