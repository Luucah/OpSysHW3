#include <stdio.h>
#include <string.h>

#define WORD_LENGTH 5

typedef enum {
    GREEN,  // Correct letter, correct position
    YELLOW, // Correct letter, wrong position
    GRAY    // Incorrect letter
} Feedback;

void evaluateWordleGuess(const char *wordle, const char *guess,
                         Feedback feedback[WORD_LENGTH]) {
    int i, j;
    int wordleUsed[WORD_LENGTH] = {0};
    int guessUsed[WORD_LENGTH] = {0};

    // Check for green feedback
    for (i = 0; i < WORD_LENGTH; i++) {
        if (wordle[i] == guess[i]) {
            feedback[i] = GREEN;
            wordleUsed[i] = 1;
            guessUsed[i] = 1;
        }
    }

    // Check for yellow feedback
    for (i = 0; i < WORD_LENGTH; i++) {
        if (!guessUsed[i]) {
            for (j = 0; j < WORD_LENGTH; j++) {
                if (!wordleUsed[j] && wordle[j] == guess[i]) {
                    feedback[i] = YELLOW;
                    wordleUsed[j] = 1;
                    guessUsed[i] = 1;
                    break;
                }
            }
        }
    }

    // Mark the rest as gray
    for (i = 0; i < WORD_LENGTH; i++) {
        if (!guessUsed[i]) {
            feedback[i] = GRAY;
        }
    }
}

int main() {
    const char *wordle = "udder";
    const char *guess = "muddy";
    Feedback feedback[WORD_LENGTH];

    evaluateWordleGuess(wordle, guess, feedback);

    for (int i = 0; i < WORD_LENGTH; i++) {
        switch (feedback[i]) {
        case GREEN:
            printf("G ");
            break;
        case YELLOW:
            printf("Y ");
            break;
        case GRAY:
            printf("X ");
            break;
        }
    }

    return 0;
}
