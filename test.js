
var assert = require('assert');
var bypass = require('./build/default/bypass');

var template_document = {
    'test': [1, 2, 3, 4],
    'another': 'sadasdadsfadfasfasff',
    'inner': {
        'blah': [123,123.123,123123123],
        'two': "asdasd asdadas"
    },
    'clone': {
        'test': [1, 2, 3, 4],
        'another': 'sadasdadsfadfasfasff',
        'inner': {
            'blah': [123,123.123,123123123],
            'two': "asdasd asdadas"
        }
    }
};

// memory cache outside of v8
var store = new bypass.BypassStore();

// create the list of documents we will put into cache
var buff = [];
for (var i=0 ; i<500000 ; ++i) {
    // this is done so the same object isn't referenced
    // cheap way to clone
    var doc = JSON.parse(JSON.stringify(template_document));
    doc.index = i;
    buff.push(doc);
}

// load the cache
buff.forEach(function(d) {
    store.set(d.index, d);
});

// read back from the cache
buff.forEach(function(d) {
    var got = store.get(d.index);
    assert.deepEqual(got, d);
});

delete buff;

store.del(1);
var got = store.get(1);
assert.equal(got, undefined);

// we deleted the local buffer, but we should still be able to get the
// object from the cache
assert.equal(store.get(2).index, 2);

