# taurus.gemspec — Ruby FFI binding for libtaurus.

Gem::Specification.new do |spec|
  spec.name          = 'taurus'
  spec.version       = '0.3.0'
  spec.summary       = 'Ruby FFI binding for libtaurus XML parser'
  spec.description   = 'Fast XML parsing, XPath evaluation, and SAX ' \
                       'via libtaurus. Pure C99 core with Ruby FFI.'
  spec.authors       = ['Ribose Inc.']
  spec.email         = ['open.source@ribose.com']
  spec.homepage      = 'https://github.com/lutaml/taurus'
  spec.license       = 'MIT'

  spec.files         = Dir['lib/**/*.rb']
  spec.require_paths = ['lib']

  spec.add_runtime_dependency 'ffi', '~> 1.0'

  spec.add_development_dependency 'rspec', '~> 3.0'

  spec.required_ruby_version = '>= 2.7'
end
