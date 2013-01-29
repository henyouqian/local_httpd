//
//  Shader.fsh
//  example
//
//  Created by Li Wei on 13-1-30.
//  Copyright (c) 2013年 Li Wei. All rights reserved.
//

varying lowp vec4 colorVarying;

void main()
{
    gl_FragColor = colorVarying;
}
