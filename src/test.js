'use strict'
const { foo } = require('../build/Release/index');
console.log(foo({ name: 'hello' }));
