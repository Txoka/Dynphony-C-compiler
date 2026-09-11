/*
 * Towers of Hanoi controller for the Turing Complete magnet puzzle.
 *
 * The first four input values are, in order:
 *   highest_disk  highest disk number in the pile (2 to 4)
 *   source        starting location (0 to 2)
 *   destination   target location (0 to 2)
 *   spare         remaining location (0 to 2)
 *
 * Disks are numbered from zero, so highest_disk == 2 means three disks.
 */

void move_one(unsigned int source, unsigned int destination) {
    output(source);      /* Move above the source pile. */
    output(5);           /* Turn the magnet on and pick up the top disk. */
    output(destination); /* Move above the destination pile. */
    output(5);           /* Turn the magnet off and release the disk. */
}

void move_pile(
    unsigned int highest_disk,
    unsigned int source,
    unsigned int destination,
    unsigned int spare
) {
    if (highest_disk != 0) {
        move_pile(highest_disk - 1, source, spare, destination);
    }

    move_one(source, destination);

    if (highest_disk != 0) {
        move_pile(highest_disk - 1, spare, destination, source);
    }
}

int main(void) {
    unsigned int highest_disk = input();
    unsigned int source = input();
    unsigned int destination = input();
    unsigned int spare = input();

    move_pile(highest_disk, source, destination, spare);
    return 0;
}
