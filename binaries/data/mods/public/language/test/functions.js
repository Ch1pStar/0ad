function number(n, object) {
  var word = lookup("nouns", object);

  if (! word) // the word couldn't be found in the dictionary
    return n+" "+object;

  if (n == 1)
    return n+" "+word.singular;
  else
    return n+" "+word.plural;
}

function test1(creature, num_d, num_i, obj, amnt) {
  return num_d+"+"+num_i+"="+(num_d*1+num_i)+". An "+creature+" buys a "+obj+" for "+amnt;
}