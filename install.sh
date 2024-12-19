# TODO: Allow for local install.sh rather than just a root one.

name="pipit"
flags="-march=native -g"

src=$([[ $(echo $(basename $(pwd))) == $name ]] && echo "src" || echo "src/$name")

cd $src
# I would like for this to not use ripgrep, but it's very convenient here.
clang $flags -o ../bin/$name $(find -name "*.c" | rg -o "[\w\d_-]+\.c$")
