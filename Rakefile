ME_DIR = File.expand_path('..', __FILE__)
LIBMP3SPLT_DIR = File.expand_path('../../libmp3splt', __FILE__)

task :clean do
  system("make distclean")
end

task :configure do
  system("./configure --prefix=#{File.join(ME_DIR, 'out')} --with-mp3splt=#{File.join(LIBMP3SPLT_DIR, 'out')}")
end

task :make do
  system("make")
end

task :install do
  system("make install")
end

task :build => [ :make, :install ]
task :rebuild => [ :clean, :build ]
task :default => [ :build ]
