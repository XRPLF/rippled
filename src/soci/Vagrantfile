# -*- mode: ruby -*-
# vi: set ft=ruby :

# Vagrant virtual development environments for SOCI

Vagrant.configure(2) do |config|
  config.vm.box = "bento/ubuntu-14.04"
  config.vm.box_check_update = true

  # Main SOCI development box with build essentials, FOSS DBs
  config.vm.define "soci", primary: true do |soci|
    soci.vm.hostname = "vmsoci"
    soci.vm.network "private_network", type: "dhcp"
    soci.vm.provider :virtualbox do |vb|
      vb.customize ["modifyvm", :id, "--memory", "1024"]
    end
    scripts = [
      "bootstrap.sh",
      "devel.sh",
      "db2cli.sh",
      "firebird.sh",
      "mysql.sh",
      "postgresql.sh",
      "build.sh"
    ]
    scripts.each { |script|
      soci.vm.provision :shell, privileged: false, :path => "scripts/vagrant/" << script
    }
  end

  # Database box with IBM DB2 Express-C
  config.vm.define "db2", autostart: false do |db2|
    db2.vm.hostname = "vmdb2"
    db2.vm.network "private_network", type: "dhcp"
    # Access to DB2 instance from host
    db2.vm.network :forwarded_port, host: 50000, guest: 50000
    db2.vm.provider :virtualbox do |vb|
      vb.customize ["modifyvm", :id, "--memory", "1024"]
    end
    scripts = [
      "bootstrap.sh",
      "db2.sh"
    ]
    scripts.each { |script|
      db2.vm.provision :shell, privileged: false, :path => "scripts/vagrant/" << script
    }
  end

  # Database box with Oracle XE
  # config.vm.define "oracle", autostart: false do |oracle|
  #   oracle.vm.provision "database", type: "shell" do |s|
  #     s.inline = "echo Installing Oracle'"
  #   end
  # end

end
