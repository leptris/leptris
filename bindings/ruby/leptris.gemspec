# leptris.gemspec — Ruby FFI binding for libleptris.

Gem::Specification.new do |spec|
  spec.name          = 'leptris'
  spec.version       = '0.3.0'
  spec.summary       = 'Ruby FFI binding for libleptris XML parser'
  spec.description   = 'Fast XML parsing, XPath evaluation, and SAX ' \
                       'via libleptris. Pure C99 core with Ruby FFI.'
  spec.authors       = ['Ribose Inc.']
  spec.email         = ['open.source@ribose.com']
  spec.homepage      = 'https://github.com/leptris/leptris'
  spec.license       = 'MIT'

  spec.files         = Dir['lib/**/*.rb']
  spec.require_paths = ['lib']

  spec.add_runtime_dependency 'ffi', '~> 1.0'

  spec.add_development_dependency 'rspec', '~> 3.0'

  spec.required_ruby_version = '>= 2.7'
end
