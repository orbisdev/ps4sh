If build fails on Mac Os install the brew version of readline:

	/usr/bin/ruby -e "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"


Then install readline

	brew install readline
	
Then proceed as usual
	make
	make install