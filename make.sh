rm -rf ./build
rm -rf ./toremove
mkdir -p build

# -O3 - максимальная оптимизация. -O0 - без оптимизации.
# -Wall -Wextra - дополнительные предупреждения

# Производим формирование английской локали как загружаемой прямо внутрь исполняемого файла.
# Это не нужно
#localecpp=locale.cpp
#rm -rf locale.cpp
#echo -e 'const char* LOCALE_EN[] =\n{' > $localecpp
#sed 's/^/"/; s/$/"/' locale/messages_en.txt | sed '$s/$/,\nnullptr\n};/' >> $localecpp

# Вот это формирование шаблона для локали
# xgettext --c++ --package-name=vinny-sdel --from-code=UTF-8 -d vinny-sdel -o locale/vinny-sdel.pot main.cpp
#msginit -i locale/vinny-sdel.pot -l ru_RU.UTF-8 -o locale/ru_RU/LC_MESSAGES/vinny-sdel.po
#msginit -i locale/vinny-sdel.pot -l en_US.UTF-8 -o locale/en_US/LC_MESSAGES/vinny-sdel.po

g++ -std=c++20 -Wall -Wextra -O3 -o ./build/sdel main.cpp
if [ $? -ne 0 ]
then
    echo
    echo "Ошибка: компиляция не удалась!" >&2
    echo
    exit 1
fi

echo "Компиляция прошла успешно"

find locale -name "*.po" -type f | while read po_file; do
    mo_file="${po_file%.po}.mo"

    rm -f $mo_file
    # msgmerge --update $po_file locale/vinny-sdel.pot
    msgfmt -c -o "$mo_file" "$po_file"  > /dev/null 2>&1
    if [ ! -f $mo_file ]
    then
        echo
        echo "for $po_file"
        #msgfmt -c -v -o "$mo_file" "$po_file"
    fi
done

echo
echo "Пробный запуск программы без параметров."


./build/sdel


echo
echo
echo "Пробный запуск программы с параметром."
echo 'aaa' >> ./build/toremove

mkdir -p toremove
ln -s ../build/toremove ./toremove/link; ln -s ../locale/ ./toremove/lc; ln -s notexists ./toremove/ne

./build/sdel tempd ./toremove -- ./toremove

r=$?
if [[ $r -ne 0 || -f $mo_file ]]
then
    echo "Провальный запуск. ec=$r"
else
    echo "Успешный запуск. ec=$r"
fi

