import os
import sys

if __name__ == "__main__":
    print("Hello, World!")
    print(sys.argv)
    print(os.environ)
    print(os.getpid())
    print(os.getgid())
    with open("./test.txt", "w") as f:
        f.write("Ну шо голова?\n")
        f.close()
